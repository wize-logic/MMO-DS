#!/bin/sh
# Online -> offline -> online, without a person, several
# times in a row.
#
#   1. reads the party and the PC as the server holds them;
#   2. boots the fused client into a session and presses Continue Offline
#      through the HUD page (openmmo-hudpush is that press with no window),
#      and expects the image and its marker where the front door would look;
#   3. plays that image with no server: the character select, a walk of five
#      tiles read off the client's own step lines, and the in-game SAVE, so
#      the file on disk is a played one and not the export;
#   4. offers the report the offline game wrote at exit, and expects "landed";
#   5. reads the containers again and expects the same species on the same
#      slots, the ids are new rows by design, the monsters are not.
set -u

ROOT=${1:?usage: offline_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: offline_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: offline_test.sh <mmo-root> <build-dir> <engine-dir>}

CLIENT="${OPENMMO_PLAY_CLIENT:-$BUILD/openmmo-client}"
FUSED="$BUILD/fused/pokeplatinum"
HUDPUSH="$BUILD/openmmo-hudpush"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
HOST="${OPENMMO_PLAY_HOST:-127.0.0.1}"
PORT="${OPENMMO_PLAY_PORT:-2106}"
GAMEPORT="${OPENMMO_PLAY_GAMEPORT:-7778}"
USER_A="${OPENMMO_PLAY_USER:-test}"
PASS_A="${OPENMMO_PLAY_PASS:-test}"
CHAR="${OPENMMO_OFFLINE_CHAR:-Offline}"
ROUNDS="${OPENMMO_OFFLINE_ROUNDS:-5}"

export OPENMMO_SERVER="$HOST:$PORT"
export OPENMMO_GAMEPORT="$GAMEPORT"

skip() { echo "offline: SKIP ($1)"; exit 0; }
[ -x "$CLIENT" ] || skip "no client at $CLIENT"
[ -x "$FUSED" ] || skip "no fused build at $FUSED"
[ -x "$HUDPUSH" ] || skip "no helper at $HUDPUSH"
[ -f "$ROM" ] || skip "no ROM at $ROM"

port_open() {
    bash -c "exec 3<>/dev/tcp/$1/$2" 2>/dev/null
}
port_open "$HOST" "$PORT" || skip "no login on $HOST:$PORT"
port_open "$HOST" "$GAMEPORT" || skip "no game on $HOST:$GAMEPORT, stand one up on 7778; see PLAY.md"

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Packages the way the front door names them, when this checkout has them; a
# save holding a species only a package draws is refused at the character
# select otherwise, by name, which is right and is not this test.
MODS=""
for m in imports followers; do
    [ -d "$ROOT/mods/$m" ] && MODS="${MODS:+$MODS,}$m"
done

CLI="$CLIENT"
AUTH="--user $USER_A --pass $PASS_A"

echo "one character goes offline and comes back, $ROUNDS times:"

# --- the character, and something in a box ---------------------------------

probe=$($CLI join $AUTH --character "$CHAR" 2>&1) || true
if printf '%s\n' "$probe" | grep -qE 'no character named|no character yet'; then
    out=$($CLI create $AUTH --name "$CHAR" --gender male --region sinnoh 2>&1) || true
    if printf '%s\n' "$out" | grep -q 'created'; then
        ok "create $CHAR as $USER_A"
    else
        printf '%s\n' "$out" | sed 's/^/       /'
        bad "create $CHAR as $USER_A"
        echo "offline: FAILED"; exit 1
    fi
elif printf '%s\n' "$probe" | grep -q "character $CHAR"; then
    ok "character $CHAR as $USER_A already there"
else
    printf '%s\n' "$probe" | tail -3 | sed 's/^/       /'
    bad "join as $CHAR"
    echo "offline: FAILED"; exit 1
fi
AUTH="$AUTH --character $CHAR"

# The containers as the server holds them: one line per monster, species and
# slot only. Ids are not compared, an import writes new rows on purpose.
containers() { # LOG -> "party 0 #399" lines on stdout, sorted
    awk '/^  party:/ { where = "party" } /^  pc:/ { where = "pc" }
         /^  daycare:/ { where = "daycare" }
         /^    slot / && where != "" { print where, $2, $3 }' "$1" | sort
}

$CLI storage $AUTH > "$tmp/seed.log" 2>&1 || true
if ! grep -q '^on join' "$tmp/seed.log"; then
    tail -3 "$tmp/seed.log" | sed 's/^/       /'
    bad "read the containers"
    echo "offline: FAILED"; exit 1
fi
party_n=$(containers "$tmp/seed.log" | grep -c '^party ')
pc_n=$(containers "$tmp/seed.log" | grep -c '^pc ')
if [ "$party_n" -lt 1 ]; then
    $CLI storage $AUTH --add bidoof --level 10 > "$tmp/seed1.log" 2>&1 || true
    grep -q 'after the move' "$tmp/seed1.log" \
        && ok "a first party member" || bad "a first party member (--add)"
fi
if [ "$pc_n" -lt 1 ]; then
    $CLI storage $AUTH --add starly --level 8 > "$tmp/seed2.log" 2>&1 || true
    $CLI storage $AUTH --deposit 1 --box 2 > "$tmp/seed3.log" 2>&1 || true
    grep -q '^    slot 30: ' "$tmp/seed3.log" \
        && ok "a monster in box 2" || bad "a monster in box 2 (--add, --deposit --box)"
fi
# A known tile with room to walk: the developer's /warp, where the account has
# it. Without it the character stands wherever it stood, and the walk below
# counts whatever steps it manages.
warp_home() {
    $CLI say "/warp twinleaf_town" $AUTH > "$tmp/warp.log" 2>&1 || true
    grep -q 'warping to' "$tmp/warp.log"
}
if warp_home; then
    ok "standing in Twinleaf Town"
else
    echo "       (no /warp for this account; walking from where it stands)"
fi

# --- the offline drive: character select, five tiles, SAVE -----------------
cat > "$tmp/walk.in" <<'EOF'
120 keys A
124 keys none
420 keys A
424 keys none
1200 keys DOWN
1212 keys none
1260 keys DOWN
1272 keys none
1320 keys DOWN
1332 keys none
1380 keys LEFT
1392 keys none
1440 keys LEFT
1452 keys none
1600 keys X
1620 keys none
1700 keys UP
1720 keys none
1800 keys UP
1820 keys none
1850 keys UP
1870 keys none
1900 keys A
1920 keys none
2100 keys A
2120 keys none
2300 keys A
2320 keys none
3000 keys A
3020 keys none
EOF

round=1
while [ "$round" -le "$ROUNDS" ]; do
    echo "round $round:"
    chan="offline-$$-$round"
    exp="$tmp/export$round.sav"
    log="$tmp/export$round.log"
    [ "$round" -gt 1 ] && warp_home

    $CLI storage $AUTH > "$tmp/before$round.log" 2>&1 || true
    containers "$tmp/before$round.log" > "$tmp/before$round.txt"
    if [ ! -s "$tmp/before$round.txt" ]; then
        bad "the containers before the export"
        break
    fi

    # 2. Continue Offline, pressed through the page.
    rm -f "/dev/shm/$chan" "/dev/shm/$chan.hud" "/dev/shm/$chan.text"
    env PC_VIEW="$chan" PC_KEEP_ALIVE=1 PC_ROM="$ROM" PC_MODS_DIR="$ROOT/mods" \
        ${MODS:+PC_MODS="$MODS"} PC_SAVE=none PC_PACE=0 PC_FRAMES=400000 \
        OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_SESSION=1 \
        OPENMMO_USER="$USER_A" OPENMMO_PASS="$PASS_A" OPENMMO_CHARACTER="$CHAR" \
        OPENMMO_EXPORT="$exp" \
        "$FUSED" > "$log" 2>&1 < /dev/null &
    pid=$!
    i=0
    while [ $i -lt 240 ]; do
        grep -q 'standing on header' "$log" && break
        kill -0 $pid 2>/dev/null || break
        sleep 0.5
        i=$((i + 1))
    done
    if ! grep -q 'standing on header' "$log"; then
        bad "the session reaches the field (export $round)"
        tail -3 "$log" | sed 's/^/       /'
        kill $pid 2>/dev/null; wait $pid 2>/dev/null
        break
    fi
    sleep 1
    "$HUDPUSH" "$chan" export > "$tmp/push$round.log" 2>&1 \
        || { bad "press Continue Offline through the page"; cat "$tmp/push$round.log" | sed 's/^/       /'; }
    i=0
    while [ $i -lt 120 ]; do
        grep -q 'leaving the server\|could not\|did not reach\|let go of' "$log" && break
        kill -0 $pid 2>/dev/null || break
        sleep 0.5
        i=$((i + 1))
    done
    i=0
    while [ $i -lt 60 ] && kill -0 $pid 2>/dev/null; do sleep 0.5; i=$((i + 1)); done
    kill $pid 2>/dev/null; wait $pid 2>/dev/null
    rm -f "/dev/shm/$chan" "/dev/shm/$chan.hud" "/dev/shm/$chan.text"
    if grep -q 'saved for offline play' "$log" && [ -s "$exp" ] \
        && grep -q "^character $CHAR\$" "$exp.ok"; then
        ok "Continue Offline writes the image and marks it for $CHAR"
    else
        bad "Continue Offline writes the image and marks it for $CHAR"
        grep -E 'openmmo: (asked|saved|the offline|leaving)' "$log" | sed 's/^/       /'
        break
    fi

    # 3. Played with no server, and saved.
    off="$tmp/offline$round.sav"
    cp "$exp" "$off"
    orc=0
    env PC_ROM="$ROM" PC_MODS_DIR="$ROOT/mods" ${MODS:+PC_MODS="$MODS"} \
        PC_SAVE="$off" PC_PACE=0 PC_FRAMES=3400 PC_INPUT="$tmp/walk.in" \
        OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_OFFLINE=1 OPENMMO_SESSION=0 \
        "$FUSED" > "$tmp/offline$round.log" 2>&1 < /dev/null || orc=$?
    steps=$(grep -c 'openmmo: step from' "$tmp/offline$round.log")
    if [ "$orc" -ne 0 ]; then
        bad "the offline game plays the image (exit $orc)"
        tail -3 "$tmp/offline$round.log" | sed 's/^/       /'
        break
    elif ! grep -q "offline character select: continuing \"$CHAR\"" "$tmp/offline$round.log"; then
        bad "the character select continues $CHAR offline"
        grep -E 'openmmo: (offline|character)' "$tmp/offline$round.log" | sed 's/^/       /'
        break
    elif [ "$steps" -lt 3 ]; then
        bad "the offline game walks ($steps step(s) taken)"
        break
    else
        ok "the offline game continues $CHAR and walks $steps tiles"
    fi
    if grep -q 'previous save kept as' "$tmp/offline$round.log" \
        && ! cmp -s "$exp" "$off"; then
        ok "SAVE from the menu writes the file"
    else
        bad "SAVE from the menu writes the file"
    fi
    if [ -s "$off.report" ]; then
        ok "the report is written beside the save at exit"
    else
        bad "the report is written beside the save at exit"
        break
    fi

    # 4. Back through the door.
    irc=0
    $CLI import-save --report "$off.report" $AUTH > "$tmp/import$round.log" 2>&1 || irc=$?
    if [ "$irc" -eq 0 ] && grep -q '^landed:' "$tmp/import$round.log"; then
        ok "the save lands on $CHAR (import $round)"
    else
        bad "the save lands on $CHAR (import $round, exit $irc)"
        grep -E '^(landed|refused|try again|error|session ended)' "$tmp/import$round.log" | sed 's/^/       /'
        break
    fi

    # 5. The same monsters on the same slots.
    $CLI storage $AUTH > "$tmp/after$round.log" 2>&1 || true
    containers "$tmp/after$round.log" > "$tmp/after$round.txt"
    if cmp -s "$tmp/before$round.txt" "$tmp/after$round.txt"; then
        ok "the party and the PC came home as they left ($(wc -l < "$tmp/after$round.txt") monster(s))"
    else
        bad "the party and the PC came home as they left"
        diff "$tmp/before$round.txt" "$tmp/after$round.txt" | sed 's/^/       /'
    fi

    # Something new for the next round, one gesture each: a monster added
    # online, boxed, taken out of the box again, let go. Each is a verb the
    # box screen has and the CLI sends (storage --add/--deposit/--withdraw/
    # --release), so four rounds cover every way a container changes.
    change=""
    case $round in
    1) change="--add starly --level 9"; said="a monster added online" ;;
    2) change="--deposit 1 --box 3"; said="a party member boxed online" ;;
    3) change="--withdraw 1 --box 3"; said="a boxed monster taken out online" ;;
    4) change="--release 1"; said="a party member released online" ;;
    esac
    if [ -n "$change" ] && [ "$round" -lt "$ROUNDS" ]; then
        $CLI storage $AUTH $change > "$tmp/change$round.log" 2>&1 || true
        grep -q 'after the move' "$tmp/change$round.log" \
            && ok "$said for the next round" \
            || bad "$said for the next round (storage $change)"
    fi
    round=$((round + 1))
done

if [ "$fail" -eq 0 ]; then
    echo "offline: OK"
    exit 0
fi
echo "offline: FAILED"
exit 1
