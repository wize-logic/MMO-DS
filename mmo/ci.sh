#!/usr/bin/env bash
# The whole client gate, on a machine that is not this one: build the -m32
# client, run its suite, and, when a login server is reachable, prove a real
# login against it.
#
#   * `link: ok` must appear and the compiled-file count must not fall below
#     OPENMMO_CI_MIN_COMPILED. `make status` exits 0 whether or not the link
#     held, it records the failure in its own output, not its exit status, so
#     the verdict is read from the lines it prints.
#   * every test must pass.
#   * the ABI drift check is reported, never gated: it SKIPs cleanly when the
#     read-only engine tree is absent (the common case on a runner), so a gate
#     on it would fail for a reason that is not the client's.
#   * the server stage gates on a real login reaching AUTHED, but only once the
#     server is up; standing it up is the runner's job (docker + a JDK), and if
#     one is already reachable this reuses it and tears nothing down.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # mmo/
REPO="$(cd "$ROOT/.." && pwd)"                          # the repo root
cd "$REPO"

DRYRUN=0
JOBS="$(nproc 2>/dev/null || echo 2)"

# Gates and knobs, all environment.
MIN_COMPILED="${OPENMMO_CI_MIN_COMPILED:-1}"
LOGIN_PORT="${OPENMMO_CI_LOGIN_PORT:-2106}"
CI_USER="${OPENMMO_CI_USER:-admin}"
CI_PASS="${OPENMMO_CI_PASS:-admin}"
SERVER_TIMEOUT="${OPENMMO_CI_SERVER_TIMEOUT:-180}"

CLIENT="mmo/build/openmmo-client"

# What the -m32 client build needs on a bare Ubuntu runner. gcc-multilib and
# the 32-bit libc are for the ABI; make and python3 drive the build and
# generators; git is the checkout.
PACKAGES=(
    build-essential gcc-multilib libc6-dev-i386
    make python3 git libsdl2-dev
    gcc-mingw-w64-i686
)

# Program -> package, for the one CI failure worth catching offline: a stage
# learns to run something and nobody adds the package.
TOOLS=(
    "gcc gcc-multilib"
    "make make"
    "python3 python3"
    "git git"
    "ld build-essential"
    "ar build-essential"
    "nm build-essential"
    "i686-w64-mingw32-gcc gcc-mingw-w64-i686"
)

say()  { printf 'ci: %s\n' "$*"; }
bad()  { printf '\033[31mci: %s\033[0m\n' "$*" >&2; }
good() { printf '\033[32mci: %s\033[0m\n' "$*"; }
die()  { bad "$*"; exit 1; }

usage() {
    cat << 'EOF'
Usage: mmo/ci.sh [--dry-run] [--jobs N] <stage> [stage...]

  packages   the apt package names this needs, one per line
  tools      the programs it runs, and the package each comes from
  build      the -m32 client build; gates on `link: ok` and the compiled count
  test       the client test suite
  abicheck   the -m32 ABI still matches the engine (reported; SKIPs if absent)
  windows    the same sources cross-built to PE; gates on the link only
  server     stand up server.login if needed, then prove a real login (gated)
  all        build, test

--dry-run prints every command each stage would run and runs none of them.
EOF
}

# Every command a stage runs is printed before it runs, plainly and with no
# colour, because --dry-run's output is parsed as well as read.
plan() { printf '+ %s\n' "$*"; }
run()  { plan "$@"; [[ "$DRYRUN" -eq 1 ]] && return 0; "$@"; }

# The numbers a reviewer wants without opening the log. Harmless anywhere else:
# outside a workflow there is no summary file and this does nothing.
summary() {
    [[ -n "${GITHUB_STEP_SUMMARY:-}" ]] || return 0
    printf '%s\n' "$*" >> "$GITHUB_STEP_SUMMARY"
}

# A TCP port is reachable, bash's own /dev/tcp, so no nc dependency.
port_open() { (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; }

stage_packages() { printf '%s\n' "${PACKAGES[@]}"; }
stage_tools()    { printf '%s\n' "${TOOLS[@]}"; }

stage_build() {
    local out compiled
    plan "make -f mmo/Makefile status -j$JOBS"
    [[ "$DRYRUN" -eq 1 ]] && return 0
    out="$(make -f mmo/Makefile status -j"$JOBS" 2>&1)"
    printf '%s\n' "$out"

    compiled="$(sed -n 's/^C files compiled: *\([0-9]*\).*/\1/p' <<< "$out" | head -1)"
    summary "build: $compiled C files compiled"

    grep -q '^link: ok' <<< "$out" || die "no binary, see mmo/build/link.log"
    [[ -n "$compiled" ]] || die "make status printed no compiled count"
    [[ "$compiled" -ge "$MIN_COMPILED" ]] \
        || die "$compiled file(s) compiled (expected at least $MIN_COMPILED)"
    good "link ok, $compiled compiled"
}

stage_test() {
    run make -f mmo/Makefile test || die "the client test suite failed"
    summary "tests: all pass"
}

# Reported, not gated: SKIPs cleanly when the read-only engine tree is absent,
# which is the common case on a runner. A gate here would fail for a reason that
# is not the client's.
stage_abicheck() {
    if [[ "$DRYRUN" -eq 1 ]]; then plan "make -f mmo/Makefile abicheck"; return 0; fi
    local out
    out="$(make -f mmo/Makefile abicheck 2>&1)"
    printf '%s\n' "$out"
    summary "abicheck: ${out##*: }"
}

# The real login. Standing the server up is the runner's job, docker for the db
# and a JDK for the server, but this owns the standup logic so it is testable
# here, and reuses a server already up rather than touching one it did not
# start. It tears down only what it started, in reverse.
stage_server() {
    [[ -x "$CLIENT" ]] || [[ "$DRYRUN" -eq 1 ]] \
        || die "no client binary at $CLIENT, run the build stage first"

    local started_gradle="" started_db=0 made_env=0 gpid=""
    _server_teardown() {
        [[ -n "$started_gradle" ]] && { plan "kill $gpid (login server this stage started)"; kill "$gpid" 2>/dev/null; wait "$gpid" 2>/dev/null; }
        [[ "$started_db" -eq 1 ]] && run docker compose stop login-db >/dev/null 2>&1
        [[ "$made_env" -eq 1 ]] && run rm -f .env
        return 0
    }

    if [[ "$DRYRUN" -eq 1 ]]; then
        plan "reuse a server already on :$LOGIN_PORT, else stand one up:"
        plan "cp .env.example .env   # if absent"
        plan "docker compose up -d login-db"
        plan "./gradlew :server.login:run &   # background; JAVA_HOME=a JDK 25"
        plan "wait for :$LOGIN_PORT (up to ${SERVER_TIMEOUT}s)"
        plan "OPENMMO_SERVER=127.0.0.1:$LOGIN_PORT $CLIENT record --out <tmp> --user $CI_USER --pass ****"
        return 0
    fi

    if port_open "$LOGIN_PORT"; then
        say "login server already up on :$LOGIN_PORT, reusing it, tearing nothing down"
    else
        say "no server on :$LOGIN_PORT, standing one up"
        if [[ ! -f .env ]]; then run cp .env.example .env; made_env=1; fi
        run docker compose up -d login-db || { _server_teardown; die "login-db would not start"; }
        started_db=1
        plan "./gradlew :server.login:run &"
        ./gradlew :server.login:run >/tmp/openmmo-login-server.log 2>&1 &
        gpid=$!; started_gradle=1
        local waited=0
        until port_open "$LOGIN_PORT"; do
            sleep 2; waited=$((waited + 2))
            kill -0 "$gpid" 2>/dev/null || { _server_teardown; die "the login server exited before it listened, see /tmp/openmmo-login-server.log"; }
            [[ "$waited" -ge "$SERVER_TIMEOUT" ]] && { _server_teardown; die "the login server never listened on :$LOGIN_PORT within ${SERVER_TIMEOUT}s"; }
        done
        say "login server up on :$LOGIN_PORT after ${waited}s"
    fi

    local trace out rc
    trace="$(mktemp)"
    # The client has no --host: the server it dials is compiled in. A build from
    # this tree still reads OPENMMO_SERVER, which is how CI aims it at the one it
    # just stood up.
    plan "OPENMMO_SERVER=127.0.0.1:$LOGIN_PORT $CLIENT record --out $trace --user $CI_USER --pass ****"
    out="$(OPENMMO_SERVER="127.0.0.1:$LOGIN_PORT" "$CLIENT" record --out "$trace" \
            --user "$CI_USER" --pass "$CI_PASS" 2>&1)"
    rc=$?
    printf '%s\n' "$out"
    rm -f "$trace"
    _server_teardown

    [[ "$rc" -eq 0 ]] || die "the login smoke did not complete (exit $rc)"
    grep -q 'response state 0' <<< "$out" \
        || die "the login did not reach AUTHED (response state 0)"
    good "live login reached AUTHED against :$LOGIN_PORT"
    summary "server: live login AUTHED on :$LOGIN_PORT"
}

# The Windows cross build, and the honest limit of it: a Linux runner can link
# a PE binary and can never run one, so the link is the gate and everything
# past it is a person on a Windows machine.
stage_windows() {
    local out compiled
    plan "make -f mmo/Makefile.win status -j$JOBS"
    if [[ "$DRYRUN" -eq 1 ]]; then
        plan "make -f mmo/Makefile.win programs -j$JOBS   # only with OPENMMO_CI_WIN_LIBS=1"
        say "nothing in this stage runs the result: no Linux runner executes a PE binary"
        return 0
    fi
    out="$(make -f mmo/Makefile.win status -j"$JOBS" 2>&1)"
    printf '%s\n' "$out"

    compiled="$(sed -n 's/^C files compiled: *\([0-9]*\).*/\1/p' <<< "$out" | head -1)"
    summary "windows: $compiled C files cross-compiled"

    grep -q '^link: ok' <<< "$out" || die "no PE binary, see mmo/build/win/link.log"
    [[ -n "$compiled" ]] || die "make status printed no compiled count"
    [[ "$compiled" -ge "$MIN_COMPILED" ]] \
        || die "$compiled file(s) cross-compiled (expected at least $MIN_COMPILED)"
    good "PE link ok, $compiled cross-compiled"

    if [[ -n "${OPENMMO_CI_WIN_LIBS:-}" ]]; then
        run make -f mmo/Makefile.win programs -j"$JOBS" \
            || die "the window or the front door would not cross-build"
        summary "windows: the window and the front door link too"
    else
        say "OPENMMO_CI_WIN_LIBS unset, the window and the front door are not built here"
    fi
}

stage_all() { stage_build && stage_test; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run) DRYRUN=1; shift;;
        --jobs)    JOBS="$2"; shift 2;;
        -h|--help) usage; exit 0;;
        -*) die "unknown option $1";;
        *)  break;;
    esac
done

[[ $# -gt 0 ]] || die "no stage named; mmo/ci.sh --help lists them"

for stage in "$@"; do
    case "$stage" in
        packages|tools|build|test|abicheck|windows|server|all) ;;
        *) die "unknown stage '$stage'; mmo/ci.sh --help lists them";;
    esac
done

for stage in "$@"; do
    "stage_$stage" || exit 1
done
