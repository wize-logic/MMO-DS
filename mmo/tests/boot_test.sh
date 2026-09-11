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
#   5b. OPENMMO_OFFLINE, the front door's offline row: its front door is this
#      game's character select, never Platinum's title. With a save the row is
#      that save's own trainer and the pick is the port's continue, the
#      seat's NEW SAVE would erase the character. With no save the row is NEW
#      GAME and the pick is the port's own opening; standing aside there was
#      the bug a fresh install hit. It also never wanders off: the port replays
#      its opening after 900 idle frames and our title does not. Without that
#      variable the same binary is the port's own title, replay and all, which
#      is what the measurement boots want.
#   5c. and a save naming a species the loaded packages cannot supply is a
#      sentence on the character select, not `fatal signal 11`: the pl_personal
#      read runs off the end of a 508-member archive and the heap it corrupts
#      takes the next free. Needs the imports package to mint one, so it SKIPs
#      without it.
#   6. no session, so grass rustles. In a session the server owns whether
#      a wild encounter happens and the field's per-step roll is cut; with
#      nobody to ask, offline play is the cartridge's own game and the
#      engine rolls again. Measured as a pair on one save: a walk up and
#      down Route 201's grass reaches a battle, and the same walk with the
#      roll forced off reaches none while taking the same hundred-odd
#      steps.
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


echo "with no server to ask, the grass rolls its own encounters:"

CONT="$ENGINE/pc/replays/lab-continue.txt"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"

if [ ! -f "$CONT" ] || [ ! -f "$SETTLE" ]; then
    echo "  SKIP (no input scripts under $ENGINE)"
else
    # Route 201, in the wide patch north of the Trainer Tips sign: the column
    # at x 143 is grass from z 842 to z 851, there is no trainer on the route
    # to start a fight of its own, and the lead cannot be knocked out. The
    # recipe's own scan of the tile it landed on is the guard, 02 is the
    # encounter behavior, so a map that moves under this fails here instead of
    # passing quietly on bare ground.
    printf 'name GRASS\nparty 445 100 0\nmap 342 143 845 1\n' > "$tmp/grass.lab"
    mrc=0
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE="$tmp/grass.sav" PC_LAB="$tmp/grass.lab" \
        PC_LAB_AT=1800 PC_PACE=0 PC_INPUT="$SETTLE" \
        PC_LAB_MAPSCAN="$tmp/grass.scan" PC_LAB_MAPSCAN_R=1 \
        "$FUSED" > "$tmp/grass.mint.log" 2>&1 || mrc=$?
    if [ "$mrc" -ne 0 ]; then
        bad "the lab mints a save standing in Route 201 grass (exit $mrc)"
        tail -2 "$tmp/grass.mint.log" | sed 's/^/       /'
    elif grep -q '^player 143 845 dir . behavior 02$' "$tmp/grass.scan"; then
        ok "the recipe stands the player on a Route 201 encounter tile"
    else
        bad "the recipe stands the player on a Route 201 encounter tile"
        sed -n '2p' "$tmp/grass.scan" | sed 's/^/       /'
    fi

    # PLAY OFFLINE's front door, on that same save: our title, then the
    # character select with the save's own trainer on it, then the pick, which
    # must be the port's CONTINUE and not the seat's NEW SAVE. The two A
    # presses are the title and the row. A save with nothing in it is the other
    # half and is checked above: no "offline title" on a plain boot, because a
    # fresh install has to be able to reach NEW GAME.
    cp "$tmp/grass.sav" "$tmp/front.sav"
    printf '120 keys A\n124 keys none\n420 keys A\n424 keys none\n' > "$tmp/front.in"
    frc=0
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_OFFLINE=1 \
        PC_ROM="$ROM" PC_SAVE="$tmp/front.sav" PC_FRAMES=2400 PC_PACE=0 \
        PC_INPUT="$tmp/front.in" \
        "$FUSED" > "$tmp/front.log" 2>&1 || frc=$?
    if [ "$frc" -ne 0 ]; then
        bad "an offline boot with a save opens the character select (exit $frc)"
        tail -2 "$tmp/front.log" | sed 's/^/       /'
    elif grep -q 'offline character select: "GRASS"' "$tmp/front.log" \
            && grep -q 'openmmo: character lobby' "$tmp/front.log"; then
        ok "an offline boot with a save opens the character select on that save"
    else
        bad "an offline boot with a save opens the character select on that save"
        grep -E 'openmmo: (offline|character|leaving)' "$tmp/front.log" | sed 's/^/       /'
    fi
    if grep -q 'leaving the lobby for the offline save' "$tmp/front.log" \
            && grep -q 'openmmo: step from\|poketch off' "$tmp/front.log"; then
        ok "and the pick continues that save rather than starting a new one"
    else
        bad "and the pick continues that save rather than starting a new one"
        grep -E 'openmmo: (leaving|offline)' "$tmp/front.log" | sed 's/^/       /'
    fi

    # And the fresh install, which is the case the first cut of this got wrong.
    # A player who unzips a build and presses PLAY OFFLINE has no save at all,
    # and standing aside for the port's title there means the one screen they
    # were promised is the one they cannot reach. The row is NEW GAME, and A on
    # it runs the cartridge's own opening rather than four creator steps that
    # would ask the name the opening is about to ask.
    nrc=0
    rm -f "$tmp/fresh.sav"
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_OFFLINE=1 \
        PC_ROM="$ROM" PC_SAVE="$tmp/fresh.sav" PC_FRAMES=1200 PC_PACE=0 \
        PC_INPUT="$tmp/front.in" \
        "$FUSED" > "$tmp/fresh.log" 2>&1 || nrc=$?
    if [ "$nrc" -ne 0 ]; then
        bad "an offline boot with no save opens the character select (exit $nrc)"
        tail -2 "$tmp/fresh.log" | sed 's/^/       /'
    elif grep -q 'nothing saved here, so NEW GAME' "$tmp/fresh.log" \
            && grep -q 'openmmo: character lobby' "$tmp/fresh.log" \
            && grep -q 'leaving the lobby for a new offline game' "$tmp/fresh.log"; then
        ok "an offline boot with no save opens the character select on NEW GAME"
    else
        bad "an offline boot with no save opens the character select on NEW GAME"
        grep -E 'openmmo: (offline|character|leaving)' "$tmp/fresh.log" | sed 's/^/       /'
    fi

    # And the same binary with nobody asking for that row is the port's own
    # title, which is what every measurement boot in this suite wants.
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE="$tmp/front.sav" PC_FRAMES=400 PC_PACE=0 \
        "$FUSED" > "$tmp/bare.log" 2>&1 || true
    if ! grep -q 'openmmo: offline title' "$tmp/bare.log"; then
        ok "and a boot nobody asked that row for is still the port's own title"
    else
        bad "and a boot nobody asked that row for is still the port's own title"
        grep -E 'openmmo: (offline|character)' "$tmp/bare.log" | sed 's/^/       /'
    fi

    # And it does not wander off. This run presses nothing at all. The title
    # hands itself to the character select the way a join does, and then the
    # row stays on our screens: the port replays its opening cutscene after 900
    # idle frames, and a player who sat in front of that would watch Nintendo,
    # GAME FREAK and the Pokemon logo, "it just boots pokemon platinum" as
    # literally as the sentence can be meant. Read off the display registers
    # rather than the picture, because these screens are animated and their
    # digests change every frame while one screen holding still keeps one
    # powcnt/dispcnt pair. Measured 2026-09-08: the offline row holds one pair
    # from 950 to 1500, and a boot that is walking the opening changes it four
    # times over the same span, which is the control below.
    steady() { # steady DUMPDIR -- "same registers at 950 and 1500" or ""
        awk '$2 != "-" && $1 + 0 == 950  { a = $3 $4 $5 }
             $2 != "-" && $1 + 0 == 1500 { b = $3 $4 $5 }
             END { if (a != "" && a == b) print "same" }' "$1/frames.txt" 2>/dev/null
    }
    rm -rf "$tmp/idle" "$tmp/wander"; mkdir -p "$tmp/idle" "$tmp/wander"
    rm -f "$tmp/idle.sav" "$tmp/wander.sav"
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_OFFLINE=1 \
        PC_ROM="$ROM" PC_SAVE="$tmp/idle.sav" PC_FRAMES=1510 PC_PACE=0 \
        PC_DUMP_FRAMES="$tmp/idle" PC_DUMP_FROM=950:50 \
        "$FUSED" > "$tmp/idle.log" 2>&1 || true
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
        PC_ROM="$ROM" PC_SAVE="$tmp/wander.sav" PC_FRAMES=1510 PC_PACE=0 \
        PC_DUMP_FRAMES="$tmp/wander" PC_DUMP_FROM=950:50 \
        "$FUSED" > "$tmp/wander.log" 2>&1 || true
    # That run pressed nothing, which is the other half of the claim: online
    # the title is a loading screen the join walks through by itself, and
    # offline the list is known before the title is even enqueued, so the
    # press was ceremony in front of the one screen this row exists to show.
    if grep -q 'leaving title for character select' "$tmp/idle.log"; then
        ok "the offline title goes to the character select with no button press"
    else
        bad "the offline title goes to the character select with no button press"
        grep -E 'openmmo: (offline|character|leaving)' "$tmp/idle.log" | sed 's/^/       /'
    fi
    if [ -n "$(steady "$tmp/idle")" ] && [ -z "$(steady "$tmp/wander")" ]; then
        ok "and it never wanders off into the opening, however long it is left"
    else
        bad "and it never wanders off into the opening, however long it is left"
        awk '$2 != "-" && $1 + 0 > 0 { print "       offline", $1, $3, $4, $5 }' \
            "$tmp/idle/frames.txt" 2>/dev/null | sed -n '1p;$p'
        awk '$2 != "-" && $1 + 0 > 0 { print "       plain  ", $1, $3, $4, $5 }' \
            "$tmp/wander/frames.txt" 2>/dev/null | sed -n '1p;$p'
    fi

    # And a save this build cannot draw is a sentence, not a crash. Minting one
    # needs the imports package, whose bytes are the player's cartridge's and
    # are not in this repository, so this SKIPs without it.
    if [ -s "$ROOT/mods/imports/mod.toml" ] \
            && [ -d "$ROOT/mods/imports/narc" ]; then
        printf 'name GEN5\nparty 634 30 0\n' > "$tmp/gen5.lab"
        grc=0
        env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
            PC_MODS_DIR="$ROOT/mods" PC_MODS=imports \
            PC_ROM="$ROM" PC_SAVE="$tmp/gen5.sav" PC_LAB="$tmp/gen5.lab" \
            PC_LAB_AT=1800 PC_FRAMES=30000 PC_PACE=0 \
            "$FUSED" > "$tmp/gen5.mint.log" 2>&1 || grc=$?
        if [ "$grc" -ne 0 ] || [ ! -f "$tmp/gen5.sav" ]; then
            bad "the lab mints a save with a Gen 5 party (exit $grc)"
            tail -2 "$tmp/gen5.mint.log" | sed 's/^/       /'
        else
            # The mint ran with the package, so it wrote a report of its own
            # on the way out. Clear it: what this asserts is that the refusing
            # run writes none.
            cp "$tmp/gen5.sav" "$tmp/gen5.orig"
            rm -f "$tmp/gen5.sav.report" "$tmp/gen5.sav.frames"
            brc=0
            env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_OFFLINE=1 \
                PC_ROM="$ROM" PC_SAVE="$tmp/gen5.sav" PC_FRAMES=900 PC_PACE=0 \
                PC_INPUT="$tmp/front.in" \
                "$FUSED" > "$tmp/gen5.log" 2>&1 || brc=$?
            if [ "$brc" -eq 0 ] \
                    && grep -q 'this save cannot be opened: SPECIES 634 NEEDS YOUR BLACK CARTRIDGE' \
                        "$tmp/gen5.log" \
                    && ! grep -q 'leaving the lobby for the offline save' "$tmp/gen5.log"; then
                ok "a save naming a species no package supplies is refused by name"
            else
                bad "a save naming a species no package supplies is refused by name (exit $brc)"
                tail -3 "$tmp/gen5.log" | sed 's/^/       /'
            fi
            if cmp -s "$tmp/gen5.sav" "$tmp/gen5.orig" \
                    && [ ! -f "$tmp/gen5.sav.report" ]; then
                ok "and the refusal leaves the save alone and offers no report"
            else
                bad "and the refusal leaves the save alone and offers no report"
            fi
        fi
    else
        echo "  SKIP (no filled mods/imports: make -C mmo import IMPORT_ROM=...)"
    fi

    # Continue, then up and down the column for the rest of the run.
    # Alternating is what keeps the player inside the patch.
    cat "$CONT" > "$tmp/graze.in"
    gf=2600
    gd=UP
    while [ "$gf" -lt 5000 ]; do
        printf '%d keys %s\n' "$gf" "$gd" >> "$tmp/graze.in"
        if [ "$gd" = UP ]; then gd=DOWN; else gd=UP; fi
        gf=$((gf + 32))
    done

    # graze TAG [LOCAL], one offline boot through that walk, echoing how many
    # commands the battle scene executed. The seam's trace is only opened when
    # there is a fight to record, so no file is no fight.
    graze() {
        cp "$tmp/grass.sav" "$tmp/$1.sav"
        env OPENMMO_SESSION=0 OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
            ${2:+OPENMMO_LOCAL_ENCOUNTERS=$2} \
            PC_ROM="$ROM" PC_SAVE="$tmp/$1.sav" PC_FRAMES=5000 PC_PACE=0 \
            PC_INPUT="$tmp/graze.in" OPENMMO_BATTLE_TRACE="$tmp/$1.trace" \
            "$FUSED" > "$tmp/$1.log" 2>&1 || echo "the $1 walk exited $?" >&2
        awk '$1 == "present"' "$tmp/$1.trace" 2>/dev/null | wc -l
    }

    lit=$(graze rustle)
    dark=$(graze quiet 0)
    walked=$(awk '/openmmo: step from/' "$tmp/quiet.log" | wc -l)

    if [ "$lit" -gt 0 ]; then
        ok "an offline walk through the grass meets a wild battle ($lit scene command(s))"
    else
        bad "an offline walk through the grass meets a wild battle"
        tail -2 "$tmp/rustle.log" | sed 's/^/       /'
    fi
    if [ "$dark" -eq 0 ] && [ "$walked" -gt 100 ]; then
        ok "and the same walk with the roll cut takes $walked steps and meets none"
    else
        bad "and the same walk with the roll cut meets none ($walked steps, $dark scene command(s))"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "boot: all checks passed"
else
    echo "boot: FAILED"
fi
exit "$fail"
