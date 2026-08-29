#!/bin/sh
# The play path, without a person.
set -eu

ROOT=${1:?usage: play_test.sh <mmo-root> <build-dir>}
BUILD=${2:?usage: play_test.sh <mmo-root> <build-dir>}

CLIENT="${OPENMMO_PLAY_CLIENT:-$BUILD/openmmo-client}"
HOST="${OPENMMO_PLAY_HOST:-127.0.0.1}"
PORT="${OPENMMO_PLAY_PORT:-2106}"
GAMEPORT="${OPENMMO_PLAY_GAMEPORT:-7778}"

# The client has no --host: the server it dials is compiled in (mmo/Makefile's
# SERVER_HOST). A build from this tree still honours these two so the suite can
# aim at a local server; a release build (SERVER_SETTABLE=0) does not read them
# at all, which is the point of them being here rather than on the command line.
export OPENMMO_SERVER="$HOST:$PORT"
export OPENMMO_GAMEPORT="$GAMEPORT"
USER_A="${OPENMMO_PLAY_USER:-test}"
PASS_A="${OPENMMO_PLAY_PASS:-test}"
USER_B="${OPENMMO_PLAY_USER2:-admin}"
PASS_B="${OPENMMO_PLAY_PASS2:-admin}"
CHAR_A="${OPENMMO_PLAY_CHAR:-PathA}"
CHAR_B="${OPENMMO_PLAY_CHAR2:-PathB}"

if [ ! -x "$CLIENT" ]; then
    echo "play: SKIP (no client at $CLIENT)"
    exit 0
fi

# A TCP port is reachable, bash's /dev/tcp, so no nc dependency. The
# same probe ci.sh uses for the login stage.
port_open() {
    bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}

if ! port_open "$HOST" "$PORT"; then
    echo "play: SKIP (no login on $HOST:$PORT)"
    exit 0
fi
if ! port_open "$HOST" "$GAMEPORT"; then
    echo "play: SKIP (no game on $HOST:$GAMEPORT, stand one up on 7778; see PLAY.md)"
    exit 0
fi

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the play path still works without a person:"

# Create only when the name is missing. A second create with the same
# name mints a duplicate row, and pair then refuses the name as ambiguous.
ensure_char() { # USER PASS NAME GENDER
    _probe=$("$CLIENT" join \
        --user "$1" --pass "$2" --character "$3" 2>&1) || true
    if printf '%s\n' "$_probe" | grep -qE 'no character named|no character yet'; then
        _out=$("$CLIENT" create \
            --user "$1" --pass "$2" --name "$3" --gender "$4" --region sinnoh 2>&1) || true
        if printf '%s\n' "$_out" | grep -q 'created'; then
            ok "create $3 as $1"
            return
        fi
        echo "$_out" | sed 's/^/       /'
        bad "create $3 as $1"
        return
    fi
    if printf '%s\n' "$_probe" | grep -q 'more than one character'; then
        bad "character $3 as $1 is not unique"
        return
    fi
    if printf '%s\n' "$_probe" | grep -q 'character '"$3"; then
        ok "character $3 as $1 already there"
        return
    fi
    echo "$_probe" | sed 's/^/       /'
    bad "probe $3 as $1"
}

ensure_char "$USER_A" "$PASS_A" "$CHAR_A" female
ensure_char "$USER_B" "$PASS_B" "$CHAR_B" male

run_pair() {
    "$CLIENT" pair \
        --user "$USER_A" --pass "$PASS_A" --user2 "$USER_B" --pass2 "$PASS_B" \
        --character "$CHAR_A" --character2 "$CHAR_B" 2>&1
}

pair_out=$(run_pair) || pair_rc=$?
pair_rc=${pair_rc:-0}
if [ "$pair_rc" -ne 0 ]; then
    # A leftover login session on admin was measured as INVALID_PASSWORD
    # on the next pair; one retry after a beat is enough.
    sleep 1
    pair_out=$(run_pair) || pair_rc=$?
    pair_rc=${pair_rc:-0}
fi

printf '%s\n' "$pair_out" | sed 's/^/       /'

if [ "$pair_rc" -eq 0 ] \
    && printf '%s\n' "$pair_out" | grep -q 'saw each other spawn, walk and talk'; then
    ok "two clients created, joined, placed, walked, saw each other and talked"
else
    bad "pair did not complete the play path (exit $pair_rc)"
fi

if [ "$fail" -eq 0 ]; then
    echo "play: all checks passed"
    exit 0
fi
echo "play: FAILED"
exit 1
