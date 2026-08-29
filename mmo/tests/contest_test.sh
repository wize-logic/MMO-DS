#!/bin/sh
# The link contest's wire, without an engine.
set -eu

ROOT=${1:?usage: contest_test.sh <mmo-root> <build-dir>}
BUILD=${2:?usage: contest_test.sh <mmo-root> <build-dir>}

CLIENT="${OPENMMO_PLAY_CLIENT:-$BUILD/openmmo-client}"
HOST="${OPENMMO_PLAY_HOST:-127.0.0.1}"
PORT="${OPENMMO_PLAY_PORT:-2106}"
GAMEPORT="${OPENMMO_PLAY_GAMEPORT:-7778}"

export OPENMMO_SERVER="$HOST:$PORT"
export OPENMMO_GAMEPORT="$GAMEPORT"

# Its own pair, not the play path's: a contest seats a character for the length
# of one and a stray seat would land in the middle of somebody's playtest.
USER_A="${OPENMMO_CONTEST_USER:-test}"
PASS_A="${OPENMMO_CONTEST_PASS:-test}"
USER_B="${OPENMMO_CONTEST_USER2:-admin}"
PASS_B="${OPENMMO_CONTEST_PASS2:-admin}"

if [ ! -x "$CLIENT" ]; then
    echo "contest: SKIP (no client at $CLIENT)"
    exit 0
fi

port_open() {
    bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}

if ! port_open "$HOST" "$PORT"; then
    echo "contest: SKIP (no login on $HOST:$PORT)"
    exit 0
fi
if ! port_open "$HOST" "$GAMEPORT"; then
    echo "contest: SKIP (no game on $HOST:$GAMEPORT, see PLAY.md)"
    exit 0
fi

echo "two players can be put in one contest:"

out=$("$CLIENT" contest \
        --user "$USER_A" --pass "$PASS_A" \
        --user2 "$USER_B" --pass2 "$PASS_B" 2>&1) || true

# The drive asks with an empty slot on purpose first, so a refusal is expected
# and is not the SKIP condition. What is: getting that far and then never being
# queued, which means slot 0 is empty too and these accounts cannot enter a
# contest at all. A machine with no such pair is not a red suite.
if printf '%s\n' "$out" | grep -q 'the queue request was refused' &&
   ! printf '%s\n' "$out" | grep -q 'queued ('; then
    echo "contest: SKIP (neither account has a monster in party slot 0)"
    exit 0
fi

fail=0
want() { # PATTERN LABEL
    if printf '%s\n' "$out" | grep -qE "$1"; then
        echo "  ok   $2"
    else
        echo "  FAIL $2"
        fail=1
    fi
}

want 'the queue request was refused'             "an empty party slot is refused on the wire"
want 'queued \([0-9]+ of [0-9]+ waiting\)'       "both sessions are told the lobby is filling"
want 'seated in contest [0-9]+ as seat [0-9]+'   "the lobby seats them rather than waiting forever"
want 'received [0-9]+ byte\(s\) from seat [0-9]+' "a broadcast reaches the other seat"
want 'both sides reported the same placements'   "an agreed result settles"
want 'the queue seated two players'              "the drive ran to the end"

if printf '%s\n' "$out" | grep -q '^error:'; then
    echo "  FAIL the drive reported an error"
    printf '%s\n' "$out" | grep '^error:' | sed 's/^/       /'
    fail=1
fi

# The other half of the story, and the one that hangs when it is wrong: a seat
# that walks out has to be announced to the seats still in it, because every
# barrier in a contest waits for every seat. Driving the happy path would never
# touch it.
drop=$("$CLIENT" contest --drop \
        --user "$USER_A" --pass "$PASS_A" \
        --user2 "$USER_B" --pass2 "$PASS_B" 2>&1) || true

if printf '%s\n' "$drop" | grep -q 'a seat that walked out was announced'; then
    echo "  ok   a seat that leaves is announced to the one still in it"
else
    echo "  FAIL a seat that leaves is announced to the one still in it"
    printf '%s\n' "$drop" | tail -12 | sed 's/^/       /'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "contest: FAILED"
    printf '%s\n' "$out" | tail -20 | sed 's/^/    /'
    exit 1
fi
echo "contest: all checks passed"
