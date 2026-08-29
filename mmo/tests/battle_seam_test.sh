#!/bin/sh
# The battle scene draws what it is handed, and it can
# be handed something from outside this process.
#
#   1. Nothing reaches the scene except through the SEAM. Every message the
#      computing half addresses to the client is recorded, every command the
#      scene executes is recorded, and the two streams pair one-for-one and in
#      order. The seam's own bookkeeping stops the run if a command is ever
#      executed that nothing delivered, so this is checked on every boot below
#      and not only in the counting here.
#   2. The same fight driven from a file presents the same stream. A recorded
#      trace fed back as the source produces a byte-identical presentation.
#   3. The bytes really come from the file. One payload word in the source is
#      changed, the seed the scene is given in its SETUP_UI message, which the
#      scene consumes and the sequence does not depend on, and the scene
#      executes the changed bytes, with every other record unmoved. Without this
#      run, 2 would pass just as well if the source were being read and thrown
#      away, because the source is what the local math produced.
#   4. The supply is the file'S to stop. A truncated source aborts the run at
#      the message it ran out on, rather than the fight carrying on locally.
#    scene executes no command. If the cut had not taken, the local fight
#    would have produced the same stream as (1).
#    recording, fed with no local emit to displace, presents the same bytes.
#    a substituting run finishes cleanly when nothing is computing a next
#    command the scene would have to keep up with.
set -eu

ROOT=${1:?usage: battle_seam_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: battle_seam_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: battle_seam_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
REPLAY="$ENGINE/pc/replays/lab-battle.txt"

# The fight, and the frames it needs. The lab starts it once the map is up and
# it is over by frame 4,358 on this save; the cap is that plus room.
BATTLE="wild 25 5"
BATTLE_AT=2600
FRAMES=4800

if [ ! -x "$FUSED" ]; then
    echo "battle seam: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ] || [ ! -f "$REPLAY" ]; then
    echo "battle seam: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# A compiled recipe, which is the numeric form the lab reads: a Garchomp and a
# Milotic at level 100 with one attacking move each, standing in Jubilife. The
# lead only has to survive the turn it takes to end the fight; if these numbers
# ever stop meaning that, the first check below finds no fight at all.
cat > "$tmp/battle.lab" <<'EOF'
name SEAM
gender 0
trainer-id 40001
money 60000
party 445 100 0
party 350 100 0
party-move 0 0 89
party-move 1 0 57
map 3 180 777 1
EOF

env PC_ROM="$ROM" PC_SAVE="$tmp/base.sav" PC_LAB="$tmp/battle.lab" \
    PC_LAB_AT=1800 PC_PACE=0 PC_INPUT="$SETTLE" \
    "$FUSED" > "$tmp/mint.log" 2>&1 || {
    echo "  FAIL the save lab could not mint a party to fight with"
    tail -3 "$tmp/mint.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
}

# fight TAG [SOURCE] [HALT], one boot into one wild battle, tracing the seam.
# Each gets its own copy of the save: the fight writes it, and a second boot
# onto a written save is a different boot.
fight() {
    _tag=$1
    _src=${2:-}
    _halt=${3:-}
    cp "$tmp/base.sav" "$tmp/$_tag.sav"
    env PC_ROM="$ROM" PC_SAVE="$tmp/$_tag.sav" PC_PACE=0 PC_INPUT="$REPLAY" \
        PC_FRAMES=$FRAMES PC_LAB_BATTLE="$BATTLE" PC_LAB_BATTLE_AT=$BATTLE_AT \
        OPENMMO_BATTLE_TRACE="$tmp/$_tag.trace" \
        OPENMMO_BATTLE_SOURCE="$_src" \
        OPENMMO_BATTLE_HALT="$_halt" \
        "$FUSED" > "$tmp/$_tag.log" 2>&1 && echo 0 || echo $?
}

echo "the battle scene is fed through one seam, and the seam can be fed:"

# 1, the fight as the engine computes it, which is also the source for the rest.
rc=$(fight local)
if [ "$rc" -ne 0 ]; then
    bad "a wild fight runs to the end with the seam recorded (exit $rc)"
    tail -3 "$tmp/local.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
fi

# Every record is `<kind> <n> <recipient> <battler> <size> <hex>`; recipient 1 is
# the client, which is the scene.
sent=$(awk '$1 == "emit" && $3 == 1' "$tmp/local.trace" | wc -l)
shown=$(grep -c '^present ' "$tmp/local.trace" || true)
if [ "$sent" -gt 0 ] && [ "$sent" = "$shown" ]; then
    ok "$sent message(s) crossed to the scene and the scene executed $shown"
else
    bad "every message to the scene is a command the scene executed ($sent sent, $shown executed)"
fi

# Pairing, not just counting: the opcode is the message's first byte, which is
# what the engine's own dispatch table indexes on.
awk '$1 == "emit" && $3 == 1 { print $4, substr($6, 1, 2) }' \
    "$tmp/local.trace" > "$tmp/sent.pairs"
awk '$1 == "present" { print $4, substr($6, 1, 2) }' \
    "$tmp/local.trace" > "$tmp/shown.pairs"
if cmp -s "$tmp/sent.pairs" "$tmp/shown.pairs"; then
    ok "each one was executed in the order it was handed over"
else
    bad "each one was executed in the order it was handed over"
    diff "$tmp/sent.pairs" "$tmp/shown.pairs" | head -6 | sed 's/^/       /'
fi

# 2, the same fight, with every message to the scene coming from the file.
rc=$(fight driven "$tmp/local.trace")
if [ "$rc" -ne 0 ]; then
    bad "the same fight runs with the scene driven from the recording (exit $rc)"
    tail -3 "$tmp/driven.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
fi

fed=$(grep -c '^feed ' "$tmp/driven.trace" || true)
if [ "$fed" = "$sent" ]; then
    ok "all $fed of them came from the file rather than from the fight"
else
    bad "all $sent of them came from the file rather than from the fight (it fed $fed)"
fi

grep '^present ' "$tmp/local.trace" > "$tmp/local.present"
grep '^present ' "$tmp/driven.trace" > "$tmp/driven.present"
if cmp -s "$tmp/local.present" "$tmp/driven.present"; then
    ok "the driven fight presented the same bytes, record for record"
else
    bad "the driven fight presented the same bytes, record for record"
    diff "$tmp/local.present" "$tmp/driven.present" | head -6 | sed 's/^/       /'
fi

# 3, the seed word inside the scene's SETUP_UI message, changed in the source
# only. Command 01 is SETUP_UI (constants/battle/battle_controller.h) and its
# payload is a command byte, three of padding and the seed the scene is told to
# draw from; nothing in the sequence below depends on its value.
sed 's/^\(emit [0-9]* 1 [0-9]* 8 01000000\)[0-9a-f]*$/\1deadbeef/' \
    "$tmp/local.trace" > "$tmp/mutated.source"
if ! grep -q 'deadbeef' "$tmp/mutated.source"; then
    bad "the source holds a SETUP_UI message to change (it does not)"
    echo "battle seam: FAILED"
    exit 1
fi

rc=$(fight mutated "$tmp/mutated.source")
if [ "$rc" -ne 0 ]; then
    bad "a fight driven from a changed source runs (exit $rc)"
    tail -3 "$tmp/mutated.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
fi

grep '^present ' "$tmp/mutated.trace" > "$tmp/mutated.present"
moved=$(diff "$tmp/local.present" "$tmp/mutated.present" | grep -c '^>' || true)
if [ "$moved" = "1" ] && grep -q '^present .* 8 01000000deadbeef$' "$tmp/mutated.present"; then
    ok "a word changed in the file is the word the scene executed"
else
    bad "a word changed in the file is the word the scene executed ($moved record(s) moved)"
    diff "$tmp/local.present" "$tmp/mutated.present" | head -6 | sed 's/^/       /'
fi

# 4, and the file is what the scene depends on: take the supply away mid-fight
# and the run stops there rather than carrying on with the local answer.
{ head -1 "$tmp/local.trace"
  awk '$1 == "emit" && $3 == 1' "$tmp/local.trace" | head -8
} > "$tmp/short.source"

rc=$(fight short "$tmp/short.source")
if [ "$rc" -ne 0 ] && grep -q 'the source ran out with the fight still going' "$tmp/short.log"; then
    ok "a source that runs out stops the fight where it ran out"
else
    bad "a source that runs out stops the fight where it ran out (exit $rc)"
    tail -3 "$tmp/short.log" | sed 's/^/       /'
fi

# 5, the arithmetic is the thing that was cut: stop it and the scene is still.
rc=$(fight halted "" 1)
if [ "$rc" -ne 0 ]; then
    bad "a fight with the arithmetic stopped runs to the frame cap (exit $rc)"
    tail -3 "$tmp/halted.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
fi
if ! grep -q 'the battle arithmetic is stopped' "$tmp/halted.log"; then
    bad "the arithmetic says it stopped"
else
    halted=$(grep -c '^present ' "$tmp/halted.trace" || true)
    if [ "$halted" = "0" ]; then
        ok "stopping the arithmetic leaves the scene with nothing to draw"
    else
        bad "stopping the arithmetic leaves the scene with nothing to draw ($halted command(s) executed)"
    fi
fi

# 6, the same recording, with no local emit to pace it.
rc=$(fight pumped "$tmp/local.trace" 1)
if [ "$rc" -ne 0 ]; then
    bad "the same fight runs with the arithmetic stopped and the scene fed from the file (exit $rc)"
    tail -3 "$tmp/pumped.log" | sed 's/^/       /'
    echo "battle seam: FAILED"
    exit 1
fi

# A local recording includes the menus that asked the player and the ai
# to pick. Those wait for an answer the arithmetic would have produced, so
# the pump skips them. What remains is the intro and the outcome, the
# commands a server-fed scene actually draws.
awk '$1 == "present" {
    op = substr($6, 1, 2)
    if (op != "0e" && op != "0f" && op != "10" && op != "11" \
        && op != "12" && op != "13" && op != "29") print
}' "$tmp/local.trace" > "$tmp/local.pumped.present"
want=$(wc -l < "$tmp/local.pumped.present")

pumped=$(grep -c '^pump ' "$tmp/pumped.trace" || true)
shown_pumped=$(grep -c '^present ' "$tmp/pumped.trace" || true)
if [ "$pumped" = "$want" ] && [ "$shown_pumped" = "$want" ]; then
    ok "all $pumped of them were delivered with no local emit to displace"
else
    bad "all $want of them were delivered with no local emit to displace (pumped $pumped, executed $shown_pumped)"
fi

grep '^present ' "$tmp/pumped.trace" > "$tmp/pumped.present"
# Seq numbers differ (the local stream had the menus in the middle); compare
# the delivered bytes and battler only.
awk '{ print $4, $5, $6 }' "$tmp/local.pumped.present" > "$tmp/local.pumped.bytes"
awk '{ print $4, $5, $6 }' "$tmp/pumped.present" > "$tmp/pumped.bytes"
if cmp -s "$tmp/local.pumped.bytes" "$tmp/pumped.bytes"; then
    ok "the pumped fight presented the same bytes, command for command"
else
    bad "the pumped fight presented the same bytes, command for command"
    diff "$tmp/local.pumped.bytes" "$tmp/pumped.bytes" | head -6 | sed 's/^/       /'
fi

# 7, a short source with the arithmetic stopped finishes; it does not abort.
rc=$(fight pumpshort "$tmp/short.source" 1)
shown_short=$(grep -c '^present ' "$tmp/pumpshort.trace" || true)
if [ "$rc" -eq 0 ] && [ "$shown_short" = "8" ] \
    && ! grep -q 'the source ran out with the fight still going' "$tmp/pumpshort.log"
then
    ok "a source that runs dry with the arithmetic stopped idles after $shown_short command(s)"
else
    bad "a source that runs dry with the arithmetic stopped idles (exit $rc, executed $shown_short)"
    tail -3 "$tmp/pumpshort.log" | sed 's/^/       /'
fi

if [ "$fail" -ne 0 ]; then
    echo "battle seam: FAILED"
    exit 1
fi
echo "battle seam: all checks passed"
