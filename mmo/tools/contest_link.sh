#!/bin/sh
# Two, three or four players in one link Super Contest,
# headless, against a live server.
#
#   contest_link.sh <mmo-root> <build> <engine> <n> [rank] [type] [outdir]
set -eu

if [ $# -lt 4 ]; then
    echo "usage: contest_link.sh <mmo-root> <build> <engine> <n> [rank] [type] [outdir]" >&2
    exit 2
fi

ROOT=$1
BUILD=$2
ENGINE=$3
N=$4
RANK=${5:-0}
TYPE=${6:-0}
OUT=${7:-$BUILD/contest-lab}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"

HOST="${OPENMMO_PLAY_HOST:-127.0.0.1}"
PORT="${OPENMMO_PLAY_PORT:-2106}"
GAMEPORT="${OPENMMO_PLAY_GAMEPORT:-7777}"

# The party slot each seat enters. A contest reads the seated party, so this is
# the seat's own lead unless it is overridden.
SLOT="${OPENMMO_CONTEST_SLOT:-0}"

# How long a seat is given. A headless run does roughly 500 frames a second, an
# official contest is about 35,000 frames of rounds, and a lobby short of four
# holds for twenty seconds of wall clock before it starts, ten thousand more
# frames. Generous rather than tight: a seat that runs out of frames mid-contest
# hangs every other seat on a barrier, which reads as a server fault.
FRAMES="${OPENMMO_CONTEST_FRAMES:-160000}"
ASK_AT="${OPENMMO_LAB_CONTEST_AT:-3000}"

# Seat accounts. The dev accounts are deliberately not here: `test` is the
# owner's own and a suite that logs into one evicts whoever is parked on it.
SEATS="${OPENMMO_CONTEST_SEATS:-probe:glassfish9:Probe probe3:Probe3:Cont3 probe4:Probe3:Probe4 probe5:Probe3:Cont5}"

if [ ! -x "$FUSED" ]; then
    echo "contest_link: no fused build at $FUSED" >&2
    exit 2
fi
if [ ! -f "$ROM" ]; then
    echo "contest_link: no ROM at $ROM" >&2
    exit 2
fi

port_open() {
    bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}
if ! port_open "$HOST" "$PORT"; then
    echo "contest_link: SKIP (no login on $HOST:$PORT)"
    exit 0
fi
if ! port_open "$HOST" "$GAMEPORT"; then
    echo "contest_link: SKIP (no game on $HOST:$GAMEPORT)"
    exit 0
fi

rm -rf "$OUT"
mkdir -p "$OUT"

# Where each seat stands, and why it has to be said every time.
WARP="${OPENMMO_CONTEST_WARP-1 155 116 886}"
CLI="${OPENMMO_CLIENT:-$BUILD/openmmo-client}"

place_seat() { # USER PASS
    [ -n "$WARP" ] || return 0
    [ -x "$CLI" ] || return 0
    printf 'connect\nawait authed\nawait in_game\nframes 120\nchat /warp %s\nframes 240\ndisconnect\n' "$WARP" \
        | OPENMMO_SERVER="$HOST:$PORT" OPENMMO_GAMEPORT="$GAMEPORT" \
          "$CLI" script --script - --mode join --user "$1" --pass "$2" \
          > "$OUT/place-$1.log" 2>&1 || true
}

i=0
pids=""
for seat in $SEATS; do
    [ "$i" -lt "$N" ] || break
    user=$(echo "$seat" | cut -d: -f1)
    pass=$(echo "$seat" | cut -d: -f2)
    char=$(echo "$seat" | cut -d: -f3)

    log="$OUT/seat$i.log"
    echo "contest_link: seat $i is $user/$char -> $log"
    place_seat "$user" "$pass"

    # Pictures, when they are asked for. Per seat, because two seats writing one
    # directory is one seat's contest with the other's frames in the gaps.
    shots=""
    if [ -n "${PC_DUMP_FRAMES:-}" ]; then
        shots="$OUT/$(basename "$PC_DUMP_FRAMES")-$i"
        mkdir -p "$shots"
    fi

    env \
        ${shots:+PC_DUMP_FRAMES="$shots"} \
        PC_ROM="$ROM" \
        PC_MODS_DIR="${PC_MODS_DIR:-$ROOT/mods}" \
        PC_MODS="${PC_MODS:-imports,followers}" \
        PC_SAVE=none \
        PC_PACE="${PC_PACE:-0}" \
        PC_FRAMES="$FRAMES" \
        PC_KEEP_ALIVE=1 \
        OPENMMO_SESSION=1 \
        OPENMMO_SERVER="$HOST:$PORT" \
        OPENMMO_GAMEPORT="$GAMEPORT" \
        OPENMMO_USER="$user" \
        OPENMMO_PASS="$pass" \
        OPENMMO_CHARACTER="$char" \
        OPENMMO_LAB_CONTEST_LINK="$RANK $TYPE $SLOT" \
        OPENMMO_LAB_CONTEST_AT="$ASK_AT" \
        setsid nohup sh -c 'echo $$ > "$1"; shift; exec "$@"' _ "$OUT/seat$i.pid" \
            "$FUSED" > "$log" 2>&1 < /dev/null &
    i=$((i + 1))
done

# The pids come out of the files rather than out of `$!`: setsid forks and
# returns at once, so `$!` is a process that has already gone and killing it
# leaves four clients running against the server for the rest of the day.
sleep 2
pids=""
for f in "$OUT"/seat*.pid; do
    [ -f "$f" ] && pids="$pids $(cat "$f")"
done

if [ "$i" -lt "$N" ]; then
    echo "contest_link: only $i account(s) in OPENMMO_CONTEST_SEATS, wanted $N" >&2
    exit 2
fi

# Wait, but on the logs rather than on the processes: a seat that has said it
# is done still has to run out its frame budget, and there is nothing to learn
# from the minutes after the contest ended.
drop_seat="${OPENMMO_CONTEST_DROP:-}"
drop_at="${OPENMMO_CONTEST_DROP_AT:-90}"
dropped=0
started=$(date +%s)

deadline=$(( $(date +%s) + ${OPENMMO_CONTEST_TIMEOUT:-1800} ))
while :; do
    if [ -n "$drop_seat" ] && [ "$dropped" -eq 0 ] \
       && [ "$(( $(date +%s) - started ))" -ge "$drop_at" ]; then
        dropped=1
        if [ -f "$OUT/seat$drop_seat.pid" ]; then
            kill -9 "$(cat "$OUT/seat$drop_seat.pid")" 2>/dev/null
            echo "contest_link: seat $drop_seat walked out"
        fi
    fi
    done_n=$(grep -l 'contest lab, done at frame' "$OUT"/seat*.log 2>/dev/null | wc -l)
    dead_n=$(grep -l 'contest lab, \(the queue request was refused\|the contest would not start\|the seat arrived but\)' "$OUT"/seat*.log 2>/dev/null | wc -l)
    [ "$((done_n + dead_n + dropped))" -ge "$N" ] && break
    [ "$(date +%s)" -ge "$deadline" ] && { echo "contest_link: timed out"; break; }
    sleep 2
done

for p in $pids; do kill "$p" 2>/dev/null || true; done
sleep 1
for p in $pids; do kill -9 "$p" 2>/dev/null || true; done

fail=0
i=0
while [ "$i" -lt "$N" ]; do
    log="$OUT/seat$i.log"
    if [ "$i" = "$drop_seat" ]; then
        echo
        echo "== seat $i (walked out on purpose)"
        i=$((i + 1))
        continue
    fi
    echo
    echo "== seat $i"
    grep -E 'contest lab|link contest|contest, |contest finished|placed' "$log" \
        | sed 's/^/   /' || true
    grep -q 'link contest .* up, seat' "$log" || { echo "   FAIL: never seated"; fail=1; }
    grep -q 'the group is set up' "$log"       || { echo "   FAIL: the opening exchange never finished"; fail=1; }
    grep -q 'link contest ended' "$log"        || { echo "   FAIL: the contest never ended"; fail=1; }
    i=$((i + 1))
done

echo
places=$(grep -h 'link contest ended' "$OUT"/seat*.log 2>/dev/null \
         | sed 's/.*placed //' | sort | tr '\n' ' ')
echo "contest_link: placements reported, one per seat: $places"
seen=$(grep -h 'seat [0-9]* of [0-9]*' "$OUT"/seat*.log | sed 's/.* of \([0-9]*\).*/\1/' | sort -u | tr '\n' ' ')
echo "contest_link: every seat says the group was of: $seen"

if [ "$fail" -ne 0 ]; then
    echo "contest_link: FAILED"
    exit 1
fi
echo "contest_link: $N seats ran one contest"
