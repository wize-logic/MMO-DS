#!/bin/sh
# The Sinnoh mainline, without a person. Retired.
set -eu

ROOT=${1:?usage: playthrough_test.sh <mmo-root> <build-dir>}
BUILD=${2:?usage: playthrough_test.sh <mmo-root> <build-dir>}

CLIENT="${OPENMMO_PLAY_CLIENT:-$BUILD/openmmo-client}"
HOST="${OPENMMO_PLAY_HOST:-127.0.0.1}"
PORT="${OPENMMO_PLAY_PORT:-2106}"
GAMEPORT="${OPENMMO_PLAY_GAMEPORT:-7778}"

# No --host on the client any more: the address is compiled in. A build from
# this tree still reads these two so the suite can aim at a local server.
export OPENMMO_SERVER="$HOST:$PORT"
export OPENMMO_GAMEPORT="$GAMEPORT"
USER="${OPENMMO_PLAY_USER:-admin}"
PASS="${OPENMMO_PLAY_PASS:-admin}"
CHAR="${OPENMMO_PLAY_CHAR:-Sinnoh}"

# The corpus this drove is gone. Nothing below can pass, and a red suite
# would say the story broke rather than that the thing measuring it did.
echo "playthrough: SKIP (the server script corpus it walked was deleted on"
echo "            2026-08-20; the story is the fused client's now and needs"
echo "            a fused-client walk, see the header of this file)"
exit 0

if [ ! -x "$CLIENT" ]; then
    echo "playthrough: SKIP (no client at $CLIENT)"
    exit 0
fi

# A TCP port is reachable, bash's /dev/tcp, so no nc dependency. The
# same probe play_test.sh uses.
port_open() {
    bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}

if ! port_open "$HOST" "$PORT"; then
    echo "playthrough: SKIP (no login on $HOST:$PORT)"
    exit 0
fi
if ! port_open "$HOST" "$GAMEPORT"; then
    echo "playthrough: SKIP (no game on $HOST:$GAMEPORT, stand one up on 7778; see PLAY.md)"
    exit 0
fi

echo "the playthrough still works without a person:"

out=$("$CLIENT" playthrough \
    --user "$USER" --pass "$PASS" --character "$CHAR" 2>&1) || rc=$?
rc=${rc:-0}

printf '%s\n' "$out" | sed 's/^/       /'

if [ "$rc" -eq 0 ] \
    && printf '%s\n' "$out" | grep -q 'playthrough: all .* gates passed'; then
    echo "playthrough: all checks passed"
    exit 0
fi
echo "playthrough: FAILED"
exit 1
