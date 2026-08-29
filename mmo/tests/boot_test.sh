#!/bin/sh
# The boot a player meets, and the name the game shows.
#
# A windowed session used to do one of two things, both wrong: walk the
# port's own opening (Nintendo / Game Freak / Pokemon, then Continue /
# New Game) or skip it with OPENMMO_BOOT_WORLD into a local new save.
# The local save is settings only, so Continue / New Game is not a
# choice this client offers, and the first thing a stranger reads has
# to be this game's name.
#
# What it defends, measured on the fused build:
#
#   1. OPENMMO_SESSION, no BOOT_WORLD: NitroMain prints "session title"
#      and does not print "booting straight into the overworld". The
#      opening is skipped. A planted .sav is dropped (PC_SAVE=none)
#      before the card is asked, so the file is neither loaded nor
#      written.
#   2. that same boot's frame 200 is not the copyright card a vanilla
#      boot is still on at frame 200 (measured: Pokemon / Nintendo /
#      GAME FREAK / ESRB). The pixels moved.
#   3. no session: a plain boot is still the port's. No "session title".
#   4. OPENMMO_BOOT_WORLD still skips to the field, even with a session,
#      so the save-lab boots keep working. It leaves the Poketch where a
#      new save leaves it, off, because there is no seat on a lab boot
#      to say the story handed one over, and the window hides that lower
#      screen whichever of the two the field puts there. The device stays
#      Vanilla: the six openmmo apps that used to be seated on it are gone
#      with the poketch mods (chat and the widget screens are the window's
#      UI layer now, the design notes), so the boot must never print "poketch take".
#      OPENMMO_MAIL arms the engine mail viewer on that same boot.
#      OPENMMO_UNION arms the communication club's join list.
#   4b. OPENMMO_WIDGET arms the kit that draws a described screen: a
#      grid, a list that scrolls past its first screenful, a pick and a
#      cancel that both leave the heap alone.
#   5. OPENMMO_FAKE_CHARS: the title leaves for the lobby, the
#      character list is empty (so NEW CHARACTER), and the naming
#      screen is not launched.
#
# SKIPs when there is no fused build or no ROM.
#
# Usage: boot_test.sh <mmo-root> <build-dir> <engine-dir>
set -eu

ROOT=${1:?usage: boot_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: boot_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: boot_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"

if [ ! -x "$FUSED" ]; then
    echo "boot: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ]; then
    echo "boot: SKIP (no ROM under $ENGINE)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# One unpaced 200-frame boot. Echoes the exit status; the caller reads
# the log and, when DIR is set, frame 200's digest.
boot() { # LOG [DIR] [VAR=VALUE ...]
    _log=$1; shift
    _dir=
    case "${1:-}" in
    /*|./*) _dir=$1; shift ;;
    esac
    env "$@" \
        PC_ROM="$ROM" PC_SAVE="$tmp/run.sav" PC_FRAMES=200 PC_PACE=0 \
        ${_dir:+PC_DUMP_FRAMES="$_dir" PC_DUMP_FROM=200} \
        "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
}

digest200() {
    awk '$1=="000200"{print $NF}' "$1/frames.txt" 2>/dev/null
}

echo "the session boot is our title, not the opening and not a local new save:"

rc=$(boot "$tmp/session.log" "$tmp/session" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1)
if [ "$rc" -ne 0 ]; then
    bad "a session boot returns (exit $rc)"
    sed -n '$p' "$tmp/session.log"
elif grep -q 'openmmo: session title' "$tmp/session.log" \
        && ! grep -q 'booting straight into the overworld' "$tmp/session.log"; then
    ok "OPENMMO_SESSION skips the opening and does not mint a local new save"
else
    bad "OPENMMO_SESSION skips the opening and does not mint a local new save"
    grep -E 'openmmo:|booting' "$tmp/session.log" || true
fi

# A planted player save must not be the session's state. The card
# prints "pc_card_rom: save: PATH (N bytes)" only when it loads one.
printf 'PLANTED-PLAYER-SAV' > "$tmp/planted.sav"
cp "$tmp/planted.sav" "$tmp/planted.orig"
env PC_ROM="$ROM" PC_SAVE="$tmp/planted.sav" PC_FRAMES=80 PC_PACE=0 \
    OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 \
    "$FUSED" > "$tmp/planted.log" 2>&1 && prc=0 || prc=$?
if [ "$prc" -ne 0 ]; then
    bad "a session boot with a planted save returns (exit $prc)"
    sed -n '$p' "$tmp/planted.log"
elif grep -q 'session is server-sided' "$tmp/planted.log" \
        && ! grep -q 'pc_card_rom: save:' "$tmp/planted.log" \
        && cmp -s "$tmp/planted.sav" "$tmp/planted.orig"; then
    ok "a session boot does not load or write a planted player save"
else
    bad "a session boot does not load or write a planted player save"
    grep -E 'pc_card_rom:|session is server-sided' "$tmp/planted.log" || true
    cmp "$tmp/planted.sav" "$tmp/planted.orig" || true
fi

rc=$(boot "$tmp/vanilla.log" "$tmp/vanilla")
if [ "$rc" -ne 0 ]; then
    bad "a plain boot returns (exit $rc)"
elif grep -q 'openmmo: session title' "$tmp/vanilla.log" \
        || grep -q 'session is server-sided' "$tmp/vanilla.log"; then
    bad "a plain boot is still the port's own opening"
elif ! grep -q 'pc_card_rom: save:' "$tmp/vanilla.log"; then
    bad "a plain boot still uses a save file"
else
    ok "a plain boot is still the port's own opening"
fi

sess=$(digest200 "$tmp/session")
vain=$(digest200 "$tmp/vanilla")
if [ -z "$sess" ] || [ -z "$vain" ]; then
    bad "both boots drew frame 200 (session '$sess', vanilla '$vain')"
elif [ "$sess" = "$vain" ]; then
    bad "frame 200 of a session boot is not the copyright card a vanilla boot is still on"
else
    ok "frame 200 of a session boot is not the copyright card a vanilla boot is still on"
fi

# The same boot with the three-process page collapsed into one process.
rc=$(boot "$tmp/oneproc.log" "$tmp/oneproc" OPENMMO_SESSION=1 \
     OPENMMO_SERVER=127.0.0.1:1 OPENMMO_ONE_PROCESS=1)
one=$(digest200 "$tmp/oneproc")
if [ "$rc" -ne 0 ]; then
    bad "a one-process boot returns (exit $rc)"
    sed -n '$p' "$tmp/oneproc.log"
elif [ -z "$one" ] || [ -z "$sess" ]; then
    bad "both boots drew frame 200 (session '$sess', one-process '$one')"
elif [ "$one" = "$sess" ]; then
    ok "collapsing the page into one process draws the same frame 200"
else
    bad "the one-process page changed the picture: $sess vs $one"
fi

rc=$(boot "$tmp/world.log" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 OPENMMO_MAIL=1 OPENMMO_UNION=1)
if [ "$rc" -ne 0 ]; then
    bad "BOOT_WORLD still returns (exit $rc)"
elif grep -q 'booting straight into the overworld' "$tmp/world.log" \
        && ! grep -q 'openmmo: session title' "$tmp/world.log"; then
    ok "OPENMMO_BOOT_WORLD still skips to the field, even with a session"
else
    bad "OPENMMO_BOOT_WORLD still skips to the field, even with a session"
    grep -E 'openmmo:|booting' "$tmp/world.log" || true
fi

if grep -q 'no poketch on this character yet' "$tmp/world.log"; then
    ok "BOOT_WORLD leaves the device off, no seat handed one over"
else
    bad "BOOT_WORLD leaves the device off, no seat handed one over"
    grep -E 'poketch' "$tmp/world.log" || true
fi

if ! grep -q 'poketch take' "$tmp/world.log" \
        && ! grep -q 'poketch app started' "$tmp/world.log"; then
    ok "no openmmo app is seated on the device, the poketch mods are gone"
else
    bad "no openmmo app is seated on the device, the poketch mods are gone"
    grep -E 'poketch' "$tmp/world.log" || true
fi

if grep -q 'mail viewer armed' "$tmp/world.log"; then
    ok "BOOT_WORLD arms the engine mail viewer"
else
    bad "BOOT_WORLD arms the engine mail viewer"
    grep -E 'mail' "$tmp/world.log" || true
fi

if grep -q 'union room armed' "$tmp/world.log"; then
    ok "BOOT_WORLD arms the union room join list"
else
    bad "BOOT_WORLD arms the union room join list"
    grep -E 'union' "$tmp/world.log" || true
fi

rc=$(boot "$tmp/widget.log" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 \
        OPENMMO_WIDGET=list)
if [ "$rc" -ne 0 ]; then
    bad "an armed widget boot returns (exit $rc)"
    sed -n '$p' "$tmp/widget.log"
elif grep -q 'widget kit armed' "$tmp/widget.log" \
        && grep -q 'openmmo: widget options from 0x5b' "$tmp/widget.log"; then
    ok "OPENMMO_WIDGET draws a described screen through the engine's own widgets"
else
    bad "OPENMMO_WIDGET draws a described screen through the engine's own widgets"
    grep -E 'widget' "$tmp/widget.log" || true
fi

# More rows than a fixed grid holds is the list, and it is the engine's
# scroll model doing the scrolling: the cursor reaches a row the first
# screenful did not show. A close that segfaults is a teardown that freed
# a window it did not allocate, so the exit status is half the check.
printf '%s\n' '150 keys DOWN' '154 keys none' '162 keys DOWN' '166 keys none' \
    '174 keys A' '178 keys none' > "$tmp/pick.in"
rc=$(boot "$tmp/pick.log" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 \
        OPENMMO_WIDGET=list PC_INPUT="$tmp/pick.in")
if [ "$rc" -ne 0 ]; then
    bad "a pick on the scrolling list returns cleanly (exit $rc)"
    sed -n '$p' "$tmp/pick.log"
elif grep -q 'openmmo: widget options from 0x5b, 1 part(s), list' "$tmp/pick.log" \
        && grep -q 'openmmo: widget options pick 2 ' "$tmp/pick.log"; then
    ok "a pick on the scrolling list returns cleanly"
else
    bad "a pick on the scrolling list returns cleanly"
    grep -E 'widget' "$tmp/pick.log" || true
fi

printf '%s\n' '160 keys B' '164 keys none' > "$tmp/cancel.in"
rc=$(boot "$tmp/cancel.log" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_BOOT_WORLD=1 \
        OPENMMO_WIDGET=1 PC_INPUT="$tmp/cancel.in")
if [ "$rc" -ne 0 ]; then
    bad "B closes the grid without taking the heap with it (exit $rc)"
    sed -n '$p' "$tmp/cancel.log"
elif grep -q 'openmmo: widget options cancelled' "$tmp/cancel.log"; then
    ok "B closes the grid without taking the heap with it"
else
    bad "B closes the grid without taking the heap with it"
    grep -E 'widget' "$tmp/cancel.log" || true
fi

rc=$(boot "$tmp/chars.log" OPENMMO_SESSION=1 OPENMMO_SERVER=127.0.0.1:1 OPENMMO_FAKE_CHARS=1)
if [ "$rc" -ne 0 ]; then
    bad "a character-select boot returns (exit $rc)"
    sed -n '$p' "$tmp/chars.log"
elif grep -q 'character select: 0 character(s)' "$tmp/chars.log" \
        && grep -q 'character lobby' "$tmp/chars.log" \
        && ! grep -qi 'NamingScreen\|gNamingScreenAppTemplate' "$tmp/chars.log"; then
    ok "the title leaves for an empty character select, not the naming screen"
else
    bad "the title leaves for an empty character select, not the naming screen"
    grep -E 'character select|character lobby|naming|Naming' "$tmp/chars.log" || true
fi

if [ "$fail" -eq 0 ]; then
    echo "boot: all checks passed"
else
    echo "boot: FAILED"
fi
exit "$fail"
