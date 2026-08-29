#!/usr/bin/env bash
# Stage a fresh machine, up to the point where ./start-server.sh works.
#
#   ./setup.sh              # install what is missing, then stage the rest
#   ./setup.sh --check      # say what is missing, change nothing
#   ./setup.sh --no-swap    # never add a swapfile, however little memory there is
#   1. the packages a build needs, a JDK 25, docker, and the handful of
#      command line tools the server scripts call
#   2. JAVA_HOME, because start-server.sh defaults it to one specific Temurin
#      build under ~/.jdks and exits when that exact path is absent
#   3. docker group membership, so the compose stack runs without sudo
#   4. swap, when memory is short and there is none: the kotlin daemon is
#      killed part way through :server.game on a machine this size without it
#   5. the decomp checkouts, which :codegen reads and cannot build without
#   6. .env, with a generated session secret, the tracked template ships that
#      one empty, and an empty value overrides the built-in default rather than
#      falling back to it, so a straight copy of the template refuses to start
#   7. the launcher config the server scripts read a game port out of
#   8. the two postgres containers
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

CHECK=0
WANT_SWAP=1
for arg in "$@"; do
    case "$arg" in
        --check)   CHECK=1 ;;
        --no-swap) WANT_SWAP=0 ;;
        -h|--help) sed -n '3,7p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)         die "unknown option $arg (--check, --no-swap, --help)" ;;
    esac
done

MISSING=0
# In --check mode nothing is done, only counted, so that the exit status says
# whether a plain run would have had work to do.
todo() { MISSING=$((MISSING + 1)); warn "would $1"; }

# The packages come from apt, so a machine without it is told what it needs
# rather than watched to fail one command at a time.
APT=0
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    [[ "${ID:-}:${ID_LIKE:-}" == *debian* ]] && APT=1
fi

# $USER is not set by every way a machine is staged, cloud-init, cron and
# `sudo -u` all run this with it empty, and under `set -u` that is fatal rather
# than merely wrong.
ME="${USER:-$(id -un)}"

SUDO=""
if (( EUID != 0 )); then
    command -v sudo >/dev/null 2>&1 || die "not root and no sudo; cannot install anything"
    SUDO="sudo"
fi

# ---------------------------------------------------------------- packages ---

# A JDK 25, because the build sets jvmToolchain(25) and will not settle for less.
have_jdk25() {
    # An empty argument would look at /bin/javac, which is a real javac on most
    # machines, so an unset JAVA_HOME would answer for one it does not name.
    local home="${1:-}"
    [[ -n "$home" && -x "$home/bin/javac" ]] \
        && "$home/bin/javac" -version 2>&1 | grep -q ' 25'
}

find_jdk25() {
    local c
    have_jdk25 "${JAVA_HOME:-}" && { echo "${JAVA_HOME}"; return 0; }
    for c in /usr/lib/jvm/java-25-openjdk-* /usr/lib/jvm/*-25-* "$HOME"/.jdks/*25*; do
        have_jdk25 "$c" && { echo "$c"; return 0; }
    done
    # A JDK already on PATH, wherever it was installed from.
    if command -v javac >/dev/null 2>&1; then
        c="$(readlink -f "$(command -v javac)")"; c="${c%/bin/javac}"
        have_jdk25 "$c" && { echo "$c"; return 0; }
    fi
    return 1
}

want=()
find_jdk25 >/dev/null 2>&1 || want+=(openjdk-25-jdk-headless)
command -v docker  >/dev/null 2>&1 || want+=(docker.io)
docker compose version >/dev/null 2>&1 || want+=(docker-compose-v2)
command -v git     >/dev/null 2>&1 || want+=(git)
command -v curl    >/dev/null 2>&1 || want+=(curl)
command -v openssl >/dev/null 2>&1 || want+=(openssl)
command -v ss      >/dev/null 2>&1 || want+=(iproute2)

if (( ${#want[@]} )); then
    if (( CHECK )); then
        todo "install: ${want[*]}"
    elif (( APT )); then
        say "installing: ${want[*]}"
        # noninteractive, or a pending-kernel prompt stalls the whole run.
        export DEBIAN_FRONTEND=noninteractive
        $SUDO apt-get update -qq
        for p in "${want[@]}"; do
            apt-cache show "$p" >/dev/null 2>&1 \
                || die "$p is not in this machine's apt sources. On a release without a
       JDK 25 package, install one from adoptium.net or sdkman.io and re-run."
        done
        $SUDO apt-get install -y -qq "${want[@]}"
    else
        die "missing: ${want[*]} -- and this is not a debian-like machine, so they
       cannot be installed from here. Install them and run this again."
    fi
else
    say "packages already in place"
fi

# ------------------------------------------------------------------- java ----

# start-server.sh reads JAVA_HOME and dies when it names nothing, so a machine
# whose JDK came from the distribution needs it spelled out once.
if (( ! CHECK )); then
    JDK="$(find_jdk25)" || die "no JDK 25 found even after installing one"
    export JAVA_HOME="$JDK"
    RC="$HOME/.bashrc"
    if ! grep -q 'openmmo: JAVA_HOME' "$RC" 2>/dev/null; then
        cat >> "$RC" <<EOF

# openmmo: JAVA_HOME, which start-server.sh needs and defaults to a path that
# only exists when the JDK came from the IntelliJ downloader.
export JAVA_HOME="$JDK"
export PATH="\$JAVA_HOME/bin:\$PATH"
EOF
        say "JAVA_HOME -> $JDK (recorded in ~/.bashrc)"
    else
        say "JAVA_HOME already recorded in ~/.bashrc"
    fi
elif JDK="$(find_jdk25 2>/dev/null)"; then
    say "JDK 25 at $JDK"
fi

# ------------------------------------------------------------------ docker ---

if (( ! CHECK )); then
    $SUDO systemctl enable --now docker >/dev/null 2>&1 || true
fi

# Compose has to run as this user, which means being in the docker group. The
# account is asked about rather than this process, because usermod does not
# reach a shell that is already open, a machine can be staged while the
# session running this still cannot see the group.
if (( EUID != 0 )) && ! id -nG "$ME" | tr ' ' '\n' | grep -qx docker; then
    if (( CHECK )); then
        todo "add $ME to the docker group"
    else
        say "adding $ME to the docker group"
        $SUDO usermod -aG docker "$ME"
        warn "log out and back in before docker works in your own shells"
    fi
fi

# Whether this process can reach docker is a separate question, and when it
# cannot the rest of the run borrows the group rather than asking for a logout
# in the middle of it.
DOCKER_SG=""
if (( EUID != 0 )) && ! id -nG | tr ' ' '\n' | grep -qx docker; then
    if command -v sg >/dev/null 2>&1; then DOCKER_SG=sg; else DOCKER_SG=sudo; fi
fi
# Run a compose command the way this run is able to.
compose() {
    case "$DOCKER_SG" in
        sg)   sg docker -c "docker compose $*" ;;
        sudo) $SUDO docker compose "$@" ;;
        *)    docker compose "$@" ;;
    esac
}

# -------------------------------------------------------------------- swap ---

mem_gib=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 / 1024 ))
has_swap=$(( $(wc -l < /proc/swaps) > 1 ))

if (( WANT_SWAP && ! has_swap && mem_gib < 10 )); then
    if (( CHECK )); then
        todo "add an 8G swapfile (${mem_gib}G of memory, no swap)"
    else
        say "adding an 8G swapfile (${mem_gib}G of memory, no swap)"
        $SUDO fallocate -l 8G /swapfile 2>/dev/null \
            || $SUDO dd if=/dev/zero of=/swapfile bs=1M count=8192 status=none
        $SUDO chmod 600 /swapfile
        $SUDO mkswap -q /swapfile >/dev/null
        $SUDO swapon /swapfile
        grep -q '^/swapfile ' /etc/fstab 2>/dev/null \
            || echo '/swapfile none swap sw 0 0' | $SUDO tee -a /etc/fstab >/dev/null
    fi
elif (( has_swap )); then
    say "swap already present"
fi

# --------------------------------------------------------------- submodules ---

# The engine is a submodule and carries Sinnoh's data with it. The two GBA
# trees are not distributed here: point DECOMP_DIR at your own checkouts, or
# clone them under decomp/ by name.
if [[ -e engine/pokeplatinum/pc ]]; then
    say "engine submodule already in place"
elif (( CHECK )); then
    todo "clone the engine submodule"
else
    say "cloning the engine submodule"
    git submodule update --init --depth 1 engine/pokeplatinum \
        || die "could not clone engine/pokeplatinum"
fi

decomp_root="${DECOMP_DIR:-$PWD/decomp}"
missing=()
for t in pokeemerald pokefirered; do
    [[ -e "$decomp_root/$t/.git" ]] || missing+=("$t")
done

if (( ${#missing[@]} == 0 )); then
    say "decomp trees already in place"
elif (( CHECK )); then
    todo "supply the decomp trees: ${missing[*]}"
else
    warn "no checkout of: ${missing[*]}

       The Hoenn and Kanto generators read them and this project does not
       distribute them. Clone each under decomp/ by name, or point DECOMP_DIR
       at a directory that already holds them:

         DECOMP_DIR=/path/to/trees ./setup.sh

       Sinnoh needs nothing: it reads the engine submodule."
fi

# --------------------------------------------------------------------- env ---

if [[ -f .env ]]; then
    say ".env already present, left alone"
elif (( CHECK )); then
    todo "write .env with a generated session secret"
else
    say "writing .env"
    cp .env.example .env
    chmod 600 .env
    # The template ships this empty, and an empty value replaces the built-in
    # default with nothing rather than falling back to it, so the server exits
    # with "server.sessionSecret must not be empty" on the first start.
    secret="$(openssl rand -base64 32)"
    sed -i "s|^OPENMMO_SESSION_SECRET=.*|OPENMMO_SESSION_SECRET=$secret|" .env
    # Local databases nobody else reaches, but changeMe! should not outlive setup.
    sed -i "s|^LOGIN_DB_PASSWORD=.*|LOGIN_DB_PASSWORD=$(openssl rand -hex 16)|" .env
    sed -i "s|^GAME_DB_PASSWORD=.*|GAME_DB_PASSWORD=$(openssl rand -hex 16)|" .env
    # A fresh checkout is a workbench, so both sides run db/dev. It creates nothing now: all it
    # does is clear out the accounts and characters older checkouts seeded.
    sed -i "s|^#LOGIN_DB_SEED_DEV=true|LOGIN_DB_SEED_DEV=true|" .env
    sed -i "s|^#GAME_DB_SEED_DEV=true|GAME_DB_SEED_DEV=true|" .env
fi

# ---------------------------------------------------------------- launcher ---

# start-server.sh, stop-server.sh, play.sh and kill-servers.sh all read the game
# port out of this file, and under `set -euo pipefail` the awk that reads it
# exits 2 when the file is absent, which takes the whole script down before it
# starts anything. The launcher writes this eventually; staging it here means
# the server scripts run on a machine the launcher has never been opened on.
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
if [[ -f "$LAUNCHER_CFG" ]]; then
    say "launcher config already present"
elif (( CHECK )); then
    todo "write $LAUNCHER_CFG"
else
    say "writing $LAUNCHER_CFG"
    mkdir -p "$(dirname "$LAUNCHER_CFG")"
    printf 'gameport 7777\n' > "$LAUNCHER_CFG"
fi

# --------------------------------------------------------------- databases ---

up="$(compose ps --services --status running 2>/dev/null || true)"
if grep -qx login-db <<<"$up" && grep -qx game-db <<<"$up"; then
    say "databases already up"
elif (( CHECK )); then
    todo "start the login-db and game-db containers"
else
    say "starting the databases"
    compose up -d login-db game-db >/dev/null
fi

# ------------------------------------------------------------------- done ----

if (( CHECK )); then
    if (( MISSING )); then
        warn "$MISSING thing(s) to stage; run ./setup.sh"
        exit 1
    fi
    say "nothing to stage, this machine is ready"
    exit 0
fi

say "staged. Next:"
say "  ./start-server.sh     login on 2106, game on 7777"
say "  ./stop-server.sh      stop both"
if [[ -n "$DOCKER_SG" ]]; then
    warn "log out and back in first, or this shell still cannot reach docker"
fi
