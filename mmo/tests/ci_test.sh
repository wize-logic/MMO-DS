#!/bin/sh
# Check the two standup scripts offline, the failures worth
# catching without a runner or a database: a CI stage learns to run a program
# and nobody adds its package, the workflow names a stage the script does not
# know, or start-server.sh decides a port or the session secret before it has
# read .env.
set -eu

CI=${1:?usage: ci_test.sh <ci.sh> <workflow.yml> <start-server.sh>}
WF=${2:?usage: ci_test.sh <ci.sh> <workflow.yml> <start-server.sh>}
SS=${3:?usage: ci_test.sh <ci.sh> <workflow.yml> <start-server.sh>}

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

run_exit() { set +e; "$@" >/dev/null 2>&1; echo $?; set -e; }

echo "the client CI gate is internally consistent:"

# 0. the script exists and runs
[ -x "$CI" ] && ok "ci.sh is present and executable" \
             || { bad "ci.sh $CI is missing or not executable"; echo "ci: FAILED"; exit 1; }

# The stages the dispatcher accepts, read from the script itself so this test
# tracks it rather than a hand-kept copy.
STAGES=$(grep -oE '^ *packages\|[a-z|]+\)' "$CI" | head -1 | tr -d ' )' | tr '|' ' ')
[ -n "$STAGES" ] && ok "the stage list parses ($STAGES)" \
                 || { bad "could not read the stage list from $CI"; echo "ci: FAILED"; exit 1; }

# 1. packages and tools print non-empty lists
pkgs=$("$CI" packages); tools=$("$CI" tools)
[ -n "$pkgs" ]  && ok "packages prints a non-empty list"  || bad "packages printed nothing"
[ -n "$tools" ] && ok "tools prints a non-empty list"     || bad "tools printed nothing"

# 2. every program's package (column 2 of tools) is one the packages stage lists
missing=""
echo "$tools" | while read -r prog pkg; do
    [ -n "$pkg" ] || continue
    printf '%s\n' "$pkgs" | grep -qx "$pkg" || echo "$prog->$pkg"
done > /tmp/ci_test_missing.$$
missing=$(cat /tmp/ci_test_missing.$$); rm -f /tmp/ci_test_missing.$$
[ -z "$missing" ] && ok "every tool's package is in the packages list" \
                  || bad "tools name packages not in the packages list: $missing"

# 3. every dry-runnable stage exits 0 and prints only planned (+ ) or ci: lines
for s in keys build test abicheck windows server oracle all; do
    out=$("$CI" --dry-run "$s" 2>&1); rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "dry-run '$s' exited $rc"
    elif printf '%s\n' "$out" | grep -qvE '^(\+ |ci: )'; then
        bad "dry-run '$s' printed a line that is neither planned nor a note"
    else
        ok "dry-run '$s' plans cleanly (exit 0)"
    fi
done

# 4. an unknown stage is refused, not silently accepted
rc=$(run_exit "$CI" nosuchstage)
[ "$rc" -ne 0 ] && ok "an unknown stage is refused (exit $rc)" \
                || bad "an unknown stage should be refused, got exit 0"

# 4a. the oracle stage generates the servers' keypair before it builds, and
# builds pinned to it.
#
#     builds pinned to it. This is the one CI failure a green suite could not
#     see: the servers make their pair per machine and the client trusts exactly
#     one root key, so a build that runs first pins somebody else's and every
#     login on that runner ends at the last line of the handshake, with nothing
#     else about the session wrong.
o_out=$("$CI" --dry-run oracle 2>&1 || true)
case "$o_out" in
    *":keys:generateGame"*) ok "the oracle stage generates the servers' keypair" ;;
    *) bad "the oracle stage generates no keypair, so it pins one it did not make" ;;
esac
case "$o_out" in
    *"SERVER_ROOT_KEY=keys/build/game.public.pem"*)
        ok "the oracle stage builds pinned to that keypair" ;;
    *)  bad "the oracle stage builds with no SERVER_ROOT_KEY: the client would trust the Makefile default and refuse this server's ServerHello" ;;
esac
gen=$(printf '%s\n' "$o_out" | grep -n ':keys:generateGame' | head -1 | cut -d: -f1)
pin=$(printf '%s\n' "$o_out" | grep -n 'SERVER_ROOT_KEY=' | head -1 | cut -d: -f1)
if [ -n "$gen" ] && [ -n "$pin" ] && [ "$gen" -lt "$pin" ]; then
    ok "the keypair is generated before the client is built"
else
    bad "the client is built before the keypair exists (generate line '$gen', pin line '$pin')"
fi

# 4b. and the plain build stage pins nothing, because the client suite's own
#     committed ServerHello vectors verify under the Makefile's default and
#     under no other key: a build stage that pinned a generated one would turn
#     `ci.sh all` red on any desk whose server key is not this repository's.
b_out=$("$CI" --dry-run build 2>&1 || true)
case "$b_out" in
    *SERVER_ROOT_KEY*) bad "the plain build stage pins a root key; the committed handshake vectors in the client suite verify only under the Makefile default" ;;
    *) ok "the plain build stage leaves the pinned key to the Makefile" ;;
esac

# 5. every stage the workflow invokes is one ci.sh knows
wf_stages=$(grep -oE 'ci\.sh +[a-z]+' "$WF" | awk '{print $2}' | sort -u)
[ -n "$wf_stages" ] && ok "the workflow invokes ci.sh ($(echo $wf_stages | tr '\n' ' '))" \
                    || bad "the workflow $WF invokes no ci.sh stage"
for s in $wf_stages; do
    case " $STAGES " in
        *" $s "*) : ;;
        *) bad "the workflow names stage '$s', which ci.sh does not define" ;;
    esac
done

echo "the standup script decides out of .env, and only out of .env:"

for f in "$CI" "$SS"; do
    bash -n "$f" 2>/dev/null && ok "$(basename "$f") parses" \
                             || bad "$(basename "$f") has a syntax error"
done

# start-server.sh cds to its own directory, so a copy of it beside a written .env
# is a whole desk: the ports and the secret it reports are the ones that .env
# asked for and nothing else. OPENMMO_IGNORE_SYSTEMD keeps an installed machine
# from handing the run over before it has decided anything.
desk=$(mktemp -d)
cp "$SS" "$desk/start-server.sh"
dry() {
    printf '%s\n' "$2" > "$desk/.env"
    set +e
    OPENMMO_IGNORE_SYSTEMD=1 OPENMMO_LOGIN_PORT= OPENMMO_GAMEPORT= \
        "$desk/start-server.sh" --dry-run > "$desk/out" 2>&1
    dry_rc=$?
    set -e
    dry_out=$(cat "$desk/out")
    case "$dry_out" in
        *"$1"*) ok "$3" ;;
        *)      bad "$3, got: $(printf '%s' "$dry_out" | tr '\n' '/')" ;;
    esac
}

# The port a client is told is the port the server has to bind: .env's, over the
# launcher config of whichever machine this happens to be.
dry "game on 7912" 'GAME_SERVER_PORT=7912
OPENMMO_SESSION_SECRET=a-real-one' "the game port is .env's GAME_SERVER_PORT"

# The secret decision is the one this script used to make before reading .env,
# when it therefore answered "nothing configured" on every machine and waived the
# refusal that guards a public signing key.
dry "the shipped one is refused" 'OPENMMO_SESSION_SECRET=a-real-one' \
    "a configured secret refuses the shipped one"
dry "the shipped one is allowed" '# no secret here' \
    "no secret at all allows the shipped one"

# Set to nothing is not the same as not set: an empty value replaces the built-in
# default with an empty string and both servers exit on "must not be empty".
dry "set to nothing" 'OPENMMO_SESSION_SECRET=' "an empty secret is refused here, not in the JVM"
[ "$dry_rc" -ne 0 ] && ok "an empty secret exits non-zero (exit $dry_rc)" \
                    || bad "an empty secret should exit non-zero, got 0"

rm -f "$desk/start-server.sh" "$desk/.env" "$desk/out"; rmdir "$desk"

echo "the CI gate writes an .env a login server will start on:"

# A runner has no .env, so the server stage writes one, and every reason the
# login server has to exit before it listens is decided in that file. A copy of
# ci.sh beside a copy of .env.example is enough of a tree to write one: the
# script cds to its own parent, and sourcing it defines its functions, the
# stage named here prints a list and starts nothing.
REPO=$(cd "$(dirname "$CI")/.." && pwd)
env_desk=$(mktemp -d)
mkdir -p "$env_desk/mmo"
cp "$CI" "$env_desk/mmo/ci.sh"
cp "$REPO/.env.example" "$env_desk/.env.example"
(cd "$env_desk" && bash -c '. mmo/ci.sh packages >/dev/null; write_ci_env >/dev/null' 2>/dev/null)
written="$env_desk/.env"

if [ -f "$written" ]; then
    ok "the server stage writes an .env from .env.example"

    # The example's last line ends without a newline, so a setting appended to it
    # lands on the end of that line and is read by nobody. The server then exits
    # over a variable the file appears to set, one line above where anyone looks.
    grep -qx 'OPENMMO_ALLOW_DEV_SECRET=1' "$written" \
        && ok "the written .env allows the secret this repository ships with, on a line of its own" \
        || bad "the written .env has no OPENMMO_ALLOW_DEV_SECRET=1 line: $(grep -c DEV_SECRET "$written") partial match(es)"

    # Empty is not unset: it replaces the built-in default with a secret of length
    # zero, which the server refuses just as flatly as the shipped one.
    if grep -qE '^[[:space:]]*OPENMMO_SESSION_SECRET[[:space:]]*=[[:space:]]*$' "$written"; then
        bad "the written .env keeps .env.example's empty OPENMMO_SESSION_SECRET line"
    else
        ok "the written .env carries no empty session secret"
    fi

    # A runner's database is empty, so the account the smoke logs in as has to be
    # one the server creates on the way up.
    grep -qx 'OPENMMO_ADMIN_USERNAME=admin' "$written" \
        && ok "the written .env names the account the login smoke uses" \
        || bad "the written .env names no admin account, so an empty database has nothing to log in as"
else
    bad "the server stage wrote no .env"
fi

# The other tree is one that already has an .env, which this reads and never
# rewrites, so the two ways it can stop the server are worth refusing up front.
check_env() {
    printf '%s\n' "$2" > "$env_desk/.env"
    set +e
    (cd "$env_desk" && bash -c '. mmo/ci.sh packages >/dev/null; check_ci_env' >/dev/null 2>&1)
    rc=$?
    set -e
    if [ "$rc" -eq "$1" ]; then ok "$3"; else bad "$3, exit $rc, wanted $1"; fi
}

check_env 1 'OPENMMO_SESSION_SECRET=' "an .env with an empty session secret is refused before the standup"
check_env 1 '# nothing about a secret at all' "an .env with no secret and no waiver is refused before the standup"
check_env 0 'OPENMMO_SESSION_SECRET=a-real-one' "an .env with a real secret is accepted"
check_env 0 'OPENMMO_ALLOW_DEV_SECRET=1' "an .env that allows the shipped secret is accepted"

rm -f "$env_desk/.env" "$env_desk/.env.example" "$env_desk/mmo/ci.sh"
rmdir "$env_desk/mmo" "$env_desk"

if [ "$fail" -eq 0 ]; then
    echo "ci: all checks passed"
else
    echo "ci: FAILED"
fi
exit $fail
