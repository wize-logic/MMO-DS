#!/bin/sh
# Content ported out of another cartridge is served over the
# player's own image, at the slot our own table says that species draws from.
#
#   1. static: the package is content and not code, a mod.toml, a recipe every
#      line of which parses, no `.c` or `.h` anywhere under it, and exactly two
#      files tracked, so a filled working tree never turns into committed
#      cartridge bytes. Never SKIPs outside a tarball.
#   2. the fill, run into a copy so the working tree keeps no cartridge bytes:
#      the driver reports the recipe's line count, refuses a ROM whose game code
#      is not the one the recipe asks for, and writes six members per line.
#   3. those members are the ones our own species table computes. `openmmo-client
#      sprite --species N` resolves a species to a character and a palette member
#      with no engine present; the porter resolves the destination name to a
#      number out of the engine tree's table. A disagreement between the two is
#      art drawn onto the wrong Pokemon, and this is where it fails.
#   4. the fused build serves it: with the package on, the game's own NARC
#      readers answer the ported bytes at that member, and the archive is still
#      2964 members, a replacement, not the append the hub makes. With it off
#      the same member is not claimed and the cartridge's own art stands.
#   5. the ported member is the same shape as the one it covers: the NCGR header
#      the engine's sprite loader reads, magic, section, tile dimensions, is
#      byte-identical to the cartridge member's, and the pixels underneath it are
#      not. A sprite that loads and a sprite that is the same picture are two
#      different claims, and this is the first one.
#   6. the player's Platinum image does not move across any of it.
#   8. the driver's own door: the three refusals that used to leave bytes
#      behind, a decomp checkout handed in place of a cartridge, a recipe
#      that fails partway, and art the porter copies and then warns will not
#      draw through this engine's sprite path.
#   7. a Gen 5 cartridge is refused, and the refusal is measured rather than
#      quoted. `--pokemon` turns one down with a member count, and a member
#      count reads like a bound somebody could widen. Read through the porter's
#      own NARC reader the archive differs in three ways and the count is only
#      the first, so there is nothing here to widen.
set -eu

ROOT=${1:?usage: import_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: import_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: import_test.sh <mmo-root> <build-dir> <engine-dir>}

PKG="$ROOT/mods/imports"
RECIPE="$PKG/port.recipe"
FUSED="$BUILD/fused/pokeplatinum"
CLIENT="$BUILD/openmmo-client"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SRC="${OPENMMO_IMPORT_ROM:-$ENGINE/../pokeheartgold/build/heartgold.us/pokeheartgold.us.nds}"
SRC5="${OPENMMO_GEN5_ROM:-$ENGINE/../pokeblack/baserom.nds}"
# The follower table's cartridge half wants any HGSS image; SoulSilver's map and
# overworld archives are byte-identical to HeartGold's (mmo/CARTRIDGES), and it
# is the one this repo has beside it. Same variable mapformat_test.sh uses.
SRC4="${OPENMMO_GEN4_ROM:-$(CDPATH= cd -- "$ROOT/.." && pwd)/roms/pokesoulsilver.nds}"
GEN5_NARC=a/0/0/4
NARC=poketool/pokegra/pl_pokegra.narc

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

stamp() { if [ -e "$1" ]; then ls -l --time-style=+%s "$1" | awk '{print $5, $6}';
          else echo absent; fi; }

# The porter is the engine's, and an engine checkout without one is a checkout
# this suite cannot measure, not a client that refused something.
PORTER="$ENGINE/pc/modport.py"
porter_missing() {
    if ! command -v python3 >/dev/null 2>&1; then
        echo "  SKIP (no python3 to run the engine's porter)"
        return 0
    fi
    if [ ! -f "$PORTER" ]; then
        echo "  SKIP (no porter at $PORTER)"
        return 0
    fi
    return 1
}

echo "the imports package is a recipe, not a cartridge:"

if [ -f "$PKG/mod.toml" ] && grep -q '^id = "imports"$' "$PKG/mod.toml"; then
    ok "mod.toml declares the package"
else
    bad "mod.toml declares the package"
fi

if find "$PKG" -name '*.c' -o -name '*.h' | grep -q .; then
    bad "the package holds no C, that is the MODS= door"
else
    ok "the package holds no C"
fi

# What the package tracks, not what is on disk: a filled package is the normal
# state of a machine that has run `make import`, and those bytes are the
# cartridge's. git is the only thing that can answer this.
tracked=$(cd "$ROOT" && git ls-files mods/imports 2>/dev/null | wc -l | tr -d ' ')
if [ "$tracked" = 0 ]; then
    echo "  SKIP (not a checkout: cannot ask what the package tracks)"
elif [ "$tracked" = 2 ]; then
    ok "the package tracks two files, and neither is cartridge bytes"
else
    bad "the package tracks two files, and neither is cartridge bytes"
    (cd "$ROOT" && git ls-files mods/imports)
fi

# Every line is <code> <kind> <source> <destination>, and nothing else.
lines=$(awk '!/^[[:space:]]*(#|$)/' "$RECIPE" | wc -l | tr -d ' ')
bad_lines=$(awk '!/^[[:space:]]*(#|$)/ &&
                 !($1 ~ /^[A-Z0-9]{4}$/ && ($2 == "pokemon" || $2 == "item_icon" || $2 == "trainer") &&
                   $3 ~ /^[a-z0-9_]+$/ && $4 ~ /^[a-z0-9_]+$/ && NF == 4)' \
            "$RECIPE" | wc -l | tr -d ' ')
if [ "$lines" -gt 0 ] && [ "$bad_lines" -eq 0 ]; then
    ok "all $lines recipe line(s) parse"
else
    bad "all recipe lines parse ($lines line(s), $bad_lines unreadable)"
    awk '!/^[[:space:]]*(#|$)/' "$RECIPE" | head -5
fi

# The registry is one file and two readers, this driver's awk and the client's
# generated table. The header is committed, so the thing that can rot is the
# committed copy: regenerate it beside the tree and insist it comes out the same.
if ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the cartridge table)"
elif python3 "$ROOT/tools/gen_cartridges.py" "$ROOT/CARTRIDGES" \
        "$tmp/cartridges.gen.h" > "$tmp/gen.log" 2>&1 \
        && cmp -s "$tmp/cartridges.gen.h" "$ROOT/src/cartridges.gen.h"; then
    ok "the committed cartridge table is what CARTRIDGES generates"
else
    bad "the committed cartridge table is what CARTRIDGES generates"
    echo "       regenerate: python3 mmo/tools/gen_cartridges.py"
    tail -2 "$tmp/gen.log"
fi

# The icon table is generated from two trees a fill never needs, so the copy
# that can rot is the committed one. SKIPs without them, like every other check
# whose input is a checkout.
icons_engine=$ENGINE
if [ ! -f "$ROOT/ITEM_ICONS" ]; then
    bad "mmo/ITEM_ICONS is committed beside the recipe that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the item-icon table)"
elif python3 "$ROOT/tools/gen_item_icons.py" "$icons_engine" \
        "$("$ROOT/tools/decomp_dir.sh" pokeheartgold 2>/dev/null)" \
        "$tmp/ITEM_ICONS" > "$tmp/icons.log" 2>&1; then
    if cmp -s "$tmp/ITEM_ICONS" "$ROOT/ITEM_ICONS"; then
        ok "the committed item-icon table is what its two trees generate"
    else
        bad "the committed item-icon table is what its two trees generate"
        echo "       regenerate: python3 mmo/tools/gen_item_icons.py"
    fi
else
    echo "  SKIP (cannot regenerate the item-icon table: $(tail -1 "$tmp/icons.log"))"
fi

# The icon table is the one Gen 5 thing that did cross.
if [ ! -f "$ROOT/POKE_ICONS" ]; then
    bad "mmo/POKE_ICONS is committed beside the tool that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the icon table)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no built image at $ROM)"
elif python3 "$ROOT/tools/porticons.py" --rom "$SRC5" --engine "$ENGINE" \
        --host-rom "$ROM" --out "$tmp/POKE_ICONS" > "$tmp/icongen.log" 2>&1; then
    if cmp -s "$tmp/POKE_ICONS" "$ROOT/POKE_ICONS"; then
        ok "the committed icon table is what a Gen 5 cartridge generates"
    else
        bad "the committed icon table is what a Gen 5 cartridge generates"
        echo "       regenerate: python3 mmo/tools/porticons.py --rom <black>"
    fi
else
    bad "the icon port's own checks hold on this cartridge"
    tail -3 "$tmp/icongen.log"
fi

# The species fill's own checks: the personal entries this game already has
# rebuilt out of the cartridge's, the name bank rewritten unchanged byte for
# byte, every letter of a ported name witnessed by a species both games hold,
# the evolution members of all 493 shared species rebuilt byte for byte bar
# the one row Gen 5 added, and the 123 ability names both games hold spelled
# identically on each side.
if ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to check the species fill)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no built image at $ROM)"
elif python3 "$ROOT/tools/portspecies.py" --rom "$SRC5" --engine "$ENGINE" \
        --host-rom "$ROM" --check > "$tmp/species.log" 2>&1; then
    ok "the species fill's oracles hold on this cartridge"
else
    bad "the species fill's oracles hold on this cartridge"
    tail -4 "$tmp/species.log"
fi

# The C fill'S own oracle, on the two images and no Python at all.
if [ ! -x "$BUILD/openmmo-launch" ]; then
    echo "  SKIP (no launcher built to run the species oracle)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no built image at $ROM to hold the shared range against)"
elif "$BUILD/openmmo-launch" --check-species "$SRC5" "$ROM" \
        > "$tmp/cspecies.log" 2>&1; then
    want="compared 493
9 477
12 5
13 1
14 8
15 5"
    if [ "$(cat "$tmp/cspecies.log")" = "$want" ]; then
        ok "the C species fill rebuilds all 493 shared species as the tool does"
    else
        bad "the C species fill rebuilds all 493 shared species as the tool does"
        echo "       want: $(echo "$want" | tr '\n' ' ')"
        echo "       have: $(tr '\n' ' ' < "$tmp/cspecies.log")"
    fi
else
    bad "the C species oracle refused"
    echo "       $(tail -1 "$tmp/cspecies.log")"
fi

# And the C fill writes what the tool writes, byte for byte, into a copy.
if [ ! -x "$BUILD/openmmo-launch" ]; then
    echo "  SKIP (no launcher built to run the species fill)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no built image at $ROM to grow the name bank from)"
elif [ ! -d "$ROOT/mods/imports/narc/poketool/personal/pl_personal.narc" ]; then
    echo "  SKIP (mods/imports is not filled; nothing to hold the C fill against)"
elif "$BUILD/openmmo-launch" --compose-species "$SRC5" "$ROM" \
        "$tmp/spc" > "$tmp/cfill.log" 2>&1; then
    same=0
    for arc in poketool/personal/pl_personal.narc poketool/personal/wotbl.narc \
               poketool/personal/evo.narc poketool/icongra/pl_poke_icon.narc; do
        if ! diff -r "$ROOT/mods/imports/narc/$arc" "$tmp/spc/narc/$arc" \
                > /dev/null 2>&1; then
            same=1
            echo "       $arc differs"
        fi
    done
    for bank in 412 413; do
        if ! cmp -s "$ROOT/mods/imports/narc/msgdata/pl_msg.narc/$bank" \
                    "$tmp/spc/narc/msgdata/pl_msg.narc/$bank"; then
            same=1
            echo "       name bank $bank differs"
        fi
    done
    # The animation carry rides the same door: it is 52 MB of the cartridge's
    # own bytes with no composition in between, so it either matches or it does
    # not, and an LZ11 decompressor that drifts would show here first.
    if "$BUILD/openmmo-launch" --compose-anim "$SRC5" "$tmp/spc" \
            >> "$tmp/cfill.log" 2>&1; then
        if ! diff -r "$ROOT/mods/imports/narc/poketool/pokegra/mmo_anim.narc" \
                     "$tmp/spc/narc/poketool/pokegra/mmo_anim.narc" \
                > /dev/null 2>&1; then
            same=1
            echo "       mmo_anim.narc differs"
        fi
    else
        same=1
        echo "       the animation carry refused: $(tail -1 "$tmp/cfill.log")"
    fi
    if [ "$same" = 0 ]; then
        ok "the C species fill writes what the tool writes, every byte"
    else
        bad "the C species fill writes what the tool writes, every byte"
    fi
else
    bad "the C species fill refused"
    echo "       $(tail -1 "$tmp/cfill.log")"
fi

# And the whole package, through the seam a play press uses.
if [ ! -x "$BUILD/openmmo-launch" ]; then
    echo "  SKIP (no launcher built to run the fill)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no built image at $ROM for the fill to append to)"
elif [ ! -d "$ROOT/mods/imports/narc/poketool/pokegra/pl_pokegra.narc" ]; then
    echo "  SKIP (mods/imports is not filled; nothing to hold the fill against)"
else
    mkdir -p "$tmp/inst/bin" "$tmp/inst/mods"
    : > "$tmp/inst/bin/pokeplatinum"
    printf 'rom %s\nrom-bw %s\nmods-dir %s\n' \
        "$ROM" "$SRC5" "$tmp/inst/mods" > "$tmp/inst/launcher.cfg"
    if OPENMMO_ROOT="$tmp/inst" "$BUILD/openmmo-launch" \
            --config "$tmp/inst/launcher.cfg" --fill-imports \
            > "$tmp/fillseam.log" 2>&1; then
        made="$tmp/inst/mods/imports"
        bad_arc=0
        for arc in poketool/pokegra/pl_pokegra.narc poketool/pokegra/height.narc \
                   poketool/waza/pl_waza_tbl.narc wazaeffect/we.arc \
                   battle/skill/waza_seq.narc; do
            if ! diff -r "$ROOT/mods/imports/narc/$arc" "$made/narc/$arc" \
                    > /dev/null 2>&1; then
                bad_arc=1
                echo "       $arc differs"
            fi
        done
        # The banks the moves and the abilities grow, and the cry pack, which
        # comes off the same cartridge through the sound archive instead.
        for bank in 0 646 647 648 610 611 612; do
            if ! cmp -s "$ROOT/mods/imports/narc/msgdata/pl_msg.narc/$bank" \
                        "$made/narc/msgdata/pl_msg.narc/$bank"; then
                bad_arc=1
                echo "       message bank $bank differs"
            fi
        done
        if ! cmp -s "$ROOT/mods/imports/cries.bin" "$made/cries.bin"; then
            bad_arc=1
            echo "       cries.bin differs"
        fi
        if [ "$bad_arc" = 0 ]; then
            ok "the fill writes the whole package the tools write, every byte"
        else
            bad "the fill writes the whole package the tools write, every byte"
        fi

        # And it is asked every press and answers from the stamp. A gate on the
        # folder existing is what let `followers` and `hgss` claim the same 189
        # members; a gate on the stamp refills when the build moves and costs a
        # stat when it has not. The second press must not rewrite a byte.
        sleep 1
        touch "$tmp/inst/mark"
        if OPENMMO_ROOT="$tmp/inst" "$BUILD/openmmo-launch" \
                --config "$tmp/inst/launcher.cfg" --fill-imports \
                >> "$tmp/fillseam.log" 2>&1 \
           && [ -z "$(find "$made" -newer "$tmp/inst/mark" -print -quit)" ]; then
            ok "a second press reads the stamp and rewrites nothing"
        else
            bad "a second press reads the stamp and rewrites nothing"
            echo "       $(tail -2 "$tmp/fillseam.log")"
        fi
        rm -rf "$tmp/inst/mods"
    else
        bad "the fill refused at the seam"
        echo "       $(tail -1 "$tmp/fillseam.log")"
    fi
fi

# And the header the client compiles is what that tool emits.
if [ ! -f "$ROOT/src/species_port.gen.h" ]; then
    bad "mmo/src/species_port.gen.h is committed beside the fill that writes it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the species port header)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif python3 "$ROOT/tools/portspecies.py" --header --out "$tmp/species_port.gen.h" \
        --rom "$SRC5" --engine "$ENGINE" \
        > "$tmp/hdr.log" 2>&1 \
        && cmp -s "$tmp/species_port.gen.h" "$ROOT/src/species_port.gen.h"; then
    ok "the committed species port header is what the fill tool emits"
    # Every one of this game's machines is accounted for, paired or explicitly
    # not: the count is the tool's own, so a change in it is a change in what
    # the two games share and wants saying rather than absorbing.
    if grep -q 'MMO_PORTED_MACHINES       100' "$ROOT/src/species_port.gen.h"; then
        ok "and it accounts for all 100 of this game's machines"
    else
        bad "and it accounts for all 100 of this game's machines"
    fi
    # The move fill freezes one of its own, and it needs no cartridge at all:
    # mmo/MOVE_ANIMS is a repository file saying which Gen 4 animation each
    # ported move borrows, and the fill on a player's machine has no checkout to
    # read it from. Held here beside the species one for the same reason.
    if [ ! -f "$ROOT/src/move_port.gen.h" ]; then
        bad "mmo/src/move_port.gen.h is committed beside the fill that writes it"
    elif python3 "$ROOT/tools/portmoves.py" --header \
            --out "$tmp/move_port.gen.h" >> "$tmp/hdr.log" 2>&1 \
            && cmp -s "$tmp/move_port.gen.h" "$ROOT/src/move_port.gen.h"; then
        ok "the committed move port header is what its tool emits"
    else
        bad "the committed move port header is what its tool emits"
        echo "       regenerate: python3 mmo/tools/portmoves.py --header"
        tail -2 "$tmp/hdr.log"
    fi
else
    bad "the committed species port header is what the fill tool emits"
    echo "       regenerate: python3 mmo/tools/portspecies.py --header"
    tail -2 "$tmp/hdr.log"
fi

if [ ! -f "$ROOT/TRAINER_GFX" ]; then
    bad "mmo/TRAINER_GFX is committed beside the recipe that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the trainer table)"
elif python3 "$ROOT/tools/gen_trainer_gfx.py" "$ENGINE" \
        "$("$ROOT/tools/decomp_dir.sh" pokeheartgold 2>/dev/null)" \
        "$tmp/TRAINER_GFX" > "$tmp/trgen.log" 2>&1; then
    if cmp -s "$tmp/TRAINER_GFX" "$ROOT/TRAINER_GFX"; then
        ok "the committed trainer table is what its two trees generate"
    else
        bad "the committed trainer table is what its two trees generate"
        echo "       regenerate: python3 mmo/tools/gen_trainer_gfx.py"
    fi
else
    echo "  SKIP (cannot regenerate the trainer table: $(tail -1 "$tmp/trgen.log"))"
fi

# The follower table is the same gate again, and then one more: its second and
# third oracles live in the cartridge rather than in a checkout, so the
# regeneration half runs for anybody and the cartridge half needs an image.
hg4=$("$ROOT/tools/decomp_dir.sh" pokeheartgold 2>/dev/null || true)
if [ ! -f "$ROOT/FOLLOWERS" ]; then
    bad "mmo/FOLLOWERS is committed beside the recipe that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the follower table)"
elif [ -z "$hg4" ] || [ ! -d "$hg4" ]; then
    echo "  SKIP (no heartgold checkout to regenerate the follower table)"
elif python3 "$ROOT/tools/gen_followers.py" "$hg4" \
        "$tmp/FOLLOWERS" > "$tmp/folgen.log" 2>&1; then
    if cmp -s "$tmp/FOLLOWERS" "$ROOT/FOLLOWERS"; then
        ok "the committed follower table is what its tree generates"
    else
        bad "the committed follower table is what its tree generates"
        echo "       regenerate: python3 mmo/tools/gen_followers.py"
    fi
else
    bad "tools/gen_followers.py refused"
    echo "       $(tail -1 "$tmp/folgen.log")"
fi

# The same table again as the three arrays the running client does arithmetic
# on. Generated from the checkout rather than from mmo/FOLLOWERS, so the two
# artifacts can be compared instead of one inheriting the other's mistakes,
# and stale is what this catches, since nothing about a .gen.h says it is old.
if [ ! -f "$ROOT/src/follower_index.gen.h" ]; then
    bad "src/follower_index.gen.h is committed beside the code that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the follower header)"
elif [ -z "$hg4" ] || [ ! -d "$hg4" ]; then
    echo "  SKIP (no heartgold checkout to regenerate the follower header)"
elif python3 "$ROOT/tools/gen_followers.py" --header "$tmp/follower_index.gen.h" \
        "$hg4" > "$tmp/folhdr.log" 2>&1; then
    if cmp -s "$tmp/follower_index.gen.h" "$ROOT/src/follower_index.gen.h"; then
        ok "the committed follower header is what its tree generates"
    else
        bad "the committed follower header is what its tree generates"
        echo "       regenerate: python3 mmo/tools/gen_followers.py --header"
    fi
else
    bad "tools/gen_followers.py --header refused"
    echo "       $(tail -1 "$tmp/folhdr.log")"
fi

# And the tables a fill needs out of a checkout, which is the half a player's
# machine cannot do for itself: which source member draws each follower, and
# the source game's own follow mode and map section per map header. Frozen so
# the launcher can fill a package from a cartridge alone, and gated here for
# the same reason as the header above, nothing about a .gen.h says it is old.
if [ ! -f "$ROOT/src/follower_fill.gen.h" ]; then
    bad "src/follower_fill.gen.h is committed beside the code that reads it"
elif ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to regenerate the follower fill tables)"
elif [ -z "$hg4" ] || [ ! -d "$hg4" ]; then
    echo "  SKIP (no heartgold checkout to regenerate the follower fill tables)"
elif python3 "$ROOT/tools/portfollow.py" --fill-header "$tmp/follower_fill.gen.h" \
        --heartgold "$hg4" > "$tmp/folfill_hdr.log" 2>&1; then
    if cmp -s "$tmp/follower_fill.gen.h" "$ROOT/src/follower_fill.gen.h"; then
        ok "the committed follower fill tables are what their tree generates"
    else
        bad "the committed follower fill tables are what their tree generates"
        echo "       regenerate: python3 mmo/tools/portfollow.py --fill-header"
    fi
else
    bad "tools/portfollow.py --fill-header refused"
    echo "       $(tail -1 "$tmp/folfill_hdr.log")"
fi

# The band has two owners and they have to agree. The porter allocates
# graphics ids from FOLLOWER_GFX_BASE and the client resolves a species to one
# with MMO_FOLLOWER_GFX_BASE.
if [ ! -f "$ROOT/src/follower_index.gen.h" ] \
        || [ ! -f "$ROOT/tools/portfollow.py" ]; then
    :
else
    py_base=$(sed -n 's/^FOLLOWER_GFX_BASE = \([0-9]*\).*/\1/p' \
        "$ROOT/tools/portfollow.py" | head -1)
    c_base=$(sed -n 's/^#define MMO_FOLLOWER_GFX_BASE *\([0-9]*\).*/\1/p' \
        "$ROOT/src/follower_index.gen.h" | head -1)
    if [ -z "$py_base" ] || [ -z "$c_base" ]; then
        bad "the follower band's base is readable from both sides"
        echo "       porter '$py_base', client '$c_base'"
    elif [ "$py_base" = "$c_base" ]; then
        ok "the porter and the client put the follower band at the same id ($c_base)"
    else
        bad "the porter and the client put the follower band at the same id"
        echo "       portfollow.py says $py_base, follower_index.gen.h says $c_base"
    fi
fi

# The cartridge half. A clean image agrees three ways; then each refusal is
# reached on purpose, because a check nothing ever trips is a check nobody has
# seen work, the argument datacheck_test.sh already makes about its own tool.
if [ ! -f "$ROOT/FOLLOWERS" ] || ! command -v python3 >/dev/null 2>&1; then
    :
elif [ -z "$hg4" ] || [ ! -d "$hg4" ]; then
    :
elif [ ! -f "$SRC4" ]; then
    echo "  SKIP (no Gen 4 cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
elif python3 "$ROOT/tools/gen_followers.py" --verify "$SRC4" "$hg4" \
        > "$tmp/folver.log" 2>&1; then
    ok "the follower art and the cartridge's own size flag agree ($(
        sed -n 's/.*sprites, \([0-9]*\) small and \([0-9]*\) large.*/\1 small, \2 large/p' \
        "$tmp/folver.log" | head -1))"
    if grep -q 'walk sequence is mmodel member' "$tmp/folver.log"; then
        ok "the follower walk sequence is one member and the image has it"
    else
        bad "the follower walk sequence was not found in the image"
    fi
    for kind in person notbtx sizeflag nosequence stray; do
        if python3 "$ROOT/tools/gen_followers.py" --verify "$SRC4" "$hg4" \
                --break "$kind" > "$tmp/folbrk.log" 2>&1; then
            ok "--break $kind is refused"
        else
            bad "--break $kind was NOT refused; that check is not working"
            echo "       $(tail -1 "$tmp/folbrk.log")"
        fi
    done
else
    bad "the follower table disagrees with the cartridge"
    sed -n '1,4p' "$tmp/folver.log" | sed 's/^/       /'
fi

# The follower fill.
echo "the follower fill carries a cartridge's whole overworld Pokemon set:"

if ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to run the follower porter)"
elif [ -z "$hg4" ] || [ ! -d "$hg4" ]; then
    echo "  SKIP (no heartgold checkout for the follower table)"
elif [ ! -f "$SRC4" ]; then
    echo "  SKIP (no Gen 4 cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
else
    # A decomp checkout is not a source, and the refusal has to come before
    # anything is written, the same door modport.sh guards.
    if python3 "$ROOT/tools/portfollow.py" --rom "$hg4" --pkg "$tmp/nope" \
            > "$tmp/folrefuse.log" 2>&1; then
        bad "a decompilation checkout is refused as a follower source"
    elif grep -q 'not a source' "$tmp/folrefuse.log" && [ ! -d "$tmp/nope" ]
    then
        ok "a decompilation checkout is refused by name, writing nothing"
    else
        bad "a decompilation checkout is refused by name, writing nothing"
        tail -1 "$tmp/folrefuse.log" | sed 's/^/       /'
    fi

    if python3 "$ROOT/tools/portfollow.py" --rom "$SRC4" \
            --pkg "$tmp/followers" --heartgold "$hg4" \
            > "$tmp/folfill.log" 2>&1; then
        ok "the porter fills a package ($(sed -n '1s/portfollow: //p' "$tmp/folfill.log"))"
    else
        bad "the porter fills a package"
        tail -2 "$tmp/folfill.log" | sed 's/^/       /'
    fi

    # The two fills are held together, which is the whole argument for
    # shipping the launcher's C one: a player has no Python and no checkout, so
    # followcompose.c fills the same package from the same cartridge, and the
    # only way that stays true is to fill it both ways and require every byte
    # to match. Same argument soundtrack_test.sh makes about its own pair.
    if [ ! -x "$BUILD/openmmo-launch" ]; then
        echo "  SKIP (no launcher built; make -C mmo launcher)"
    elif "$BUILD/openmmo-launch" --compose-followers "$SRC4" \
            "$tmp/cfollowers" > "$tmp/folc.log" 2>&1; then
        if diff -r "$tmp/followers/.cooked" "$tmp/cfollowers/.cooked" \
                > "$tmp/foldiff.log" 2>&1 \
           && cmp -s "$tmp/followers/mod.toml" "$tmp/cfollowers/mod.toml"; then
            ok "the launcher's fill is the porter's, byte for byte"
        else
            bad "the launcher's fill is the porter's, byte for byte"
            head -3 "$tmp/foldiff.log" | sed 's/^/       /'
        fi
    else
        bad "the launcher's own follower fill runs"
        tail -2 "$tmp/folc.log" | sed 's/^/       /'
    fi

    # The BASE is allocated, NOT A constant, and this is the check that a
    # follower package and a ported region can be loaded together at all. Both
    # append to data/mmodel/mmodel.narc; pc_modfs claims by absolute index and
    # refuses both a collision and a gap, so the follower fill has to start one
    # past whatever else is loaded, and has to go back to the image's own
    # count when nothing else is. A synthetic sibling claiming 470..658 stands
    # in for `hgss` here, so the check needs no second cartridge and does not
    # care whether this machine has a ported region.
    mkdir -p "$tmp/sib/other/.cooked/narc/data/mmodel/mmodel.narc"
    i=470
    while [ "$i" -le 658 ]; do
        : > "$tmp/sib/other/.cooked/narc/data/mmodel/mmodel.narc/$i"
        i=$((i + 1))
    done
    printf 'id = "other"\n' > "$tmp/sib/other/mod.toml"

    if [ ! -x "$BUILD/openmmo-launch" ]; then
        echo "  SKIP (no launcher built; make -C mmo launcher)"
    else
        got=$("$BUILD/openmmo-launch" --follower-base "$tmp/sib" "other")
        if [ "$got" = "659 201" ]; then
            ok "the fill allocates past a package that claims 470..658"
        else
            bad "the fill allocates past a package that claims 470..658" \
                "wanted '659 201', got '$got'"
        fi
        got=$("$BUILD/openmmo-launch" --follower-base "$tmp/sib" "nothing")
        if [ "$got" = "470 201" ]; then
            ok "and falls back to the image's own count when nothing claims"
        else
            bad "and falls back to the image's own count when nothing claims" \
                "wanted '470 201', got '$got'"
        fi
        # The package must not allocate around itself: a refill would walk up
        # the archive a band at a time and the second one would leave a hole.
        mkdir -p "$tmp/sib/followers/.cooked/narc/data/mmodel/mmodel.narc"
        : > "$tmp/sib/followers/.cooked/narc/data/mmodel/mmodel.narc/1037"
        got=$("$BUILD/openmmo-launch" --follower-base "$tmp/sib" "other,followers")
        if [ "$got" = "659 201" ]; then
            ok "and a refill does not allocate around its own last fill"
        else
            bad "and a refill does not allocate around its own last fill" \
                "wanted '659 201', got '$got'"
        fi

        # A looks package that has already been filled at 1038..1067, so the
        # two assertions below are about a row that is really there: an absent
        # directory claims nothing and would pass either way.
        mkdir -p "$tmp/sib/looks/.cooked/narc/data/mmodel/mmodel.narc"
        : > "$tmp/sib/looks/.cooked/narc/data/mmodel/mmodel.narc/1067"

        # The two composed packages are allocated in an order, and the looks
        # fill is the one that comes second: it has to count the follower
        # fill's claims. Skipping a `looks` row for both callers, right for
        # the follower fill, which must not chase a base that moves when it
        # moves, handed the looks fill the follower fill's own base. Both
        # then claimed mmodel 470..499, the later load won every one of them,
        # and the field died on the first billboard whose sequence asked the
        # substituted body for a texture it does not carry, for every player
        # who had given us a Heart Gold.
        got=$("$BUILD/openmmo-launch" --follower-base "$tmp/sib" "other,followers,looks" looks)
        if [ "$got" = "1038 201" ]; then
            ok "the looks fill allocates past the follower fill, not onto it"
        else
            bad "the looks fill allocates past the follower fill, not onto it" \
                "wanted '1038 201', got '$got'"
        fi
        # And the order holds from the other side: the follower fill's answer
        # does not move when a looks package is sitting there.
        got=$("$BUILD/openmmo-launch" --follower-base "$tmp/sib" "other,followers,looks" followers)
        if [ "$got" = "659 201" ]; then
            ok "and the follower fill still does not chase the looks fill"
        else
            bad "and the follower fill still does not chase the looks fill" \
                "wanted '659 201', got '$got'"
        fi
    fi

    # A REFILL AT A new BASE leaves nothing of the old one. This is the bug the
    # allocation created and the one that made the moved base a lie: a fill
    # writes members by index, so a refill at a different base adds a band
    # rather than replacing one. Measured before the fix: 757 members instead
    # of 568, the stale 470..658 still claiming the ported region's, and the
    # stamp saying the collision had been fixed.
    if [ ! -x "$BUILD/openmmo-launch" ]; then
        :
    elif "$BUILD/openmmo-launch" --compose-followers "$SRC4" "$tmp/refill" \
            470 201 > "$tmp/refill1.log" 2>&1 \
         && "$BUILD/openmmo-launch" --compose-followers "$SRC4" "$tmp/refill" \
            659 201 > "$tmp/refill2.log" 2>&1; then
        d="$tmp/refill/.cooked/narc/data/mmodel/mmodel.narc"
        n=$(ls "$d" | wc -l)
        lo=$(ls "$d" | sort -n | head -1)
        hi=$(ls "$d" | sort -n | tail -1)
        if [ "$n" = "568" ] && [ "$lo" = "659" ] && [ "$hi" = "1226" ]; then
            ok "a refill at a new base leaves none of the old one behind"
        else
            bad "a refill at a new base leaves none of the old one behind" \
                "$n members running $lo..$hi, wanted 568 running 659..1226"
        fi
        if [ "$(cat "$tmp/refill/composed.txt")" = "v2 566 659 201" ]; then
            ok "and the stamp names the base it was actually filled at"
        else
            bad "and the stamp names the base it was actually filled at" \
                "$(cat "$tmp/refill/composed.txt")"
        fi
    else
        bad "a package refills over itself"
        tail -1 "$tmp/refill2.log" | sed 's/^/       /'
    fi

    # And the two fills still agree AT the moved BASE, which is the half that
    # says the allocation is the same arithmetic on both sides and not two.
    if [ ! -x "$BUILD/openmmo-launch" ]; then
        :
    elif python3 "$ROOT/tools/portfollow.py" --rom "$SRC4" \
            --pkg "$tmp/movedpy" --heartgold "$hg4" --after "$tmp/sib/other" \
            > "$tmp/folmoved.log" 2>&1 \
         && "$BUILD/openmmo-launch" --compose-followers "$SRC4" \
            "$tmp/movedc" 659 201 > "$tmp/folmovedc.log" 2>&1; then
        if diff -r "$tmp/movedpy/.cooked" "$tmp/movedc/.cooked" \
                > "$tmp/movediff.log" 2>&1; then
            ok "both fills agree at the moved base, byte for byte"
        else
            bad "both fills agree at the moved base, byte for byte"
            head -3 "$tmp/movediff.log" | sed 's/^/       /'
        fi
    else
        bad "both fills run at a moved base"
        tail -1 "$tmp/folmoved.log" "$tmp/folmovedc.log" | sed 's/^/       /'
    fi

    # And it is reached: filled at Play out of the player's own cartridge and
    # named in the mods list without anybody typing it, which is the half that
    # makes it a shipped feature rather than a developer's recipe.
    if grep -Fq 'followcompose.c' "$ROOT/Makefile" \
       && grep -Fq 'mmo_followcompose_ensure' "$ROOT/launcher/launcher.c" \
       && grep -Fq '"followers"' "$ROOT/launcher/launch_plan.c"; then
        ok "the launcher fills and names the package at Play"
    else
        bad "the launcher fills and names the package at Play" \
            "followcompose is not built in, or start_play no longer asks it"
    fi

    # And the handheld does both too. Linking the filler is not calling it:
    # the app linked followcompose.c cleanly for a whole afternoon while its
    # front door composed only the soundtrack, so an RG556 got no follower and
    # nothing said so. Both halves are named here because either one alone is
    # the same silence, a fill nobody loads, or a name with nothing behind it.
    if grep -Fq 'followcompose.c' "$ROOT/Makefile.android" \
       && grep -Fq 'mmo_followcompose_ensure' \
                "$ROOT/android/src/mmo_frontdoor_launch.c" \
       && grep -Fq 'fdl_compose_followers' "$ROOT/android/src/mmo_frontdoor.c" \
       && grep -Fq '"followers"' "$ROOT/android/src/mmo_frontdoor.c"; then
        ok "and the handheld's front door fills and names it too"
    else
        bad "and the handheld's front door fills and names it too" \
            "the app links the filler without calling it, or never names the package"
    fi

    # And a package can be put there AT all, which is a separate claim from
    # naming one. The app's mods folder used to be created by whichever
    # composer ran first, through mkdir's 0755, and the device's storage layer
    # left it group r-x, so `adb push` and every file manager were refused
    # while the folder sat plainly in `ls`, and a handheld that had pressed
    # Play once could never be handed a content package. One folder, made at
    # the door, at the mode the directory above it already has.
    if grep -Fq 'fd_mods_root' "$ROOT/android/src/mmo_frontdoor.c" \
       && grep -Eq 'mkdir\(out, 0770\)|chmod\(out, 0770\)' \
                "$ROOT/android/src/mmo_frontdoor.c"; then
        ok "and the handheld's packages folder can be written into"
    else
        bad "and the handheld's packages folder can be written into" \
            "the app makes mods/ at a mode nothing but the app can write"
    fi

    # And a world package can be asked for. `hgss` is left out of the
    # self-naming list on purpose on both platforms, which region a player
    # walks into is a choice somebody makes, and the desktop makes it in the
    # typed `mods` row. The app has no typed row, so the choice had nowhere to
    # live and a staged `hgss` was read by nothing at all. Both halves again:
    # a row that stores the answer, and a launch that names the package.
    if grep -Fq '"World"' "$ROOT/android/src/mmo_frontdoor.c" \
       && grep -Fq 's->world' "$ROOT/android/src/mmo_frontdoor.c" \
       && grep -Fq 'hgss/mod.toml' "$ROOT/android/src/mmo_frontdoor.c" \
       && grep -Fq '"hgss"' "$ROOT/android/src/mmo_frontdoor.c"; then
        ok "and the handheld can ask for the world package"
    else
        bad "and the handheld can ask for the world package" \
            "the app has no World row, or the row names no package"
    fi

    # Every member the tables name is the cartridge's own bytes, and the shiny
    # sequence is the walk with its palette run rewritten and nothing else.
    if python3 - "$ROOT" "$SRC4" "$tmp/followers" > "$tmp/folback.log" 2>&1 \
            <<'PY'
import struct, sys
from pathlib import Path

root, rom_path, pkg = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
sys.path.insert(0, str(root / "tools"))
import importlib.util                                        # noqa: E402
spec = importlib.util.spec_from_file_location(
    "gf", root / "tools" / "gen_followers.py")
gf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gf)
sys.path.insert(0, str(gf._engine_pc()))
from modport import NitroRom                                 # noqa: E402

mm = NitroRom(Path(rom_path)).narc_members(gf.SRC_MMODEL)
narc = pkg / ".cooked/narc/data/mmodel/mmodel.narc"
gfx = [l.split() for l in
       (pkg / ".cooked/generated/billboard_gfx.txt").read_text().split("\n") if l]
seq = [l.split() for l in
       (pkg / ".cooked/generated/billboard_seq.txt").read_text().split("\n") if l]

members = sorted(int(p.name) for p in narc.iterdir())
if members != list(range(members[0], members[0] + len(members))):
    raise SystemExit("the appended members have a hole in them")

# Every planted member is a member of the cartridge, byte for byte.
carried = {int(r[1]) for r in gfx}
for m in carried:
    blob = (narc / str(m)).read_bytes()
    if blob not in mm:
        raise SystemExit("planted member %d is not any member of the image" % m)

# The two sequences: one carried, one the same with its palette run set to 1.
if len(seq) != 2:
    raise SystemExit("expected a walk and a shiny sequence, got %d" % len(seq))
walk = (narc / seq[0][1]).read_bytes()
shiny = (narc / seq[1][1]).read_bytes()
n = struct.unpack_from("<I", walk, 0)[0]
if len(walk) != len(shiny) or walk[:4 + 3 * n] != shiny[:4 + 3 * n]:
    raise SystemExit("the shiny sequence changed more than its palette run")
if set(walk[4 + 3 * n:4 + 4 * n]) != {0} or set(shiny[4 + 3 * n:4 + 4 * n]) != {1}:
    raise SystemExit("the palette runs are not all-0 and all-1")

# Two gfx rows per member exactly when the shiny band is on, and the shiny row
# is the normal one plus the count, on the same member and the other sequence.
half = len(gfx) // 2
for i in range(half):
    a, b = gfx[i], gfx[i + half]
    if a[1] != b[1] or a[2] != b[2] or a[3] == b[3]:
        raise SystemExit("row %s and its shiny %s do not pair" % (a, b))
    if int(b[0]) - int(a[0]) != half:
        raise SystemExit("the shiny band is not the normal band plus %d" % half)

# The talk'S header, and the two tables A FOLLOWER'S rules come out of.
# The follow mode is HeartGold's own two-bit field carried per source header,
# so what is checked is that it decodes to the three values that field has and
# nothing else, a byte outside them would be a header this reader misparsed
# rather than a map with an opinion we have not seen.
talk = pkg / ".cooked/narc/openmmo/follow_talk.narc"
if talk.is_dir():
    head = (talk / "0").read_bytes()
    if len(head) < 56:
        raise SystemExit("the talk header is %d bytes, not 56" % len(head))
    magic, version = struct.unpack_from("<IH", head, 0)
    if magic != 0x3154464F:
        raise SystemExit("the talk header's magic is %08x" % magic)
    tp_base = struct.unpack_from("<H", head, 34)[0]
    mode_base = struct.unpack_from("<H", head, 38)[0]
    ball = struct.unpack_from("<HHH", head, 50)
    tp = (talk / str(tp_base)).read_bytes()
    modes = (talk / str(mode_base)).read_bytes()

    # HeartGold's tp_param, four bytes a sprite: byte 1 is the size flag and
    # byte 2 the walk-dip class, both read by the follower's own step code.
    if len(tp) != 4 * (len(gfx) // 2):
        raise SystemExit("%d tp_param bytes for %d sprites"
                         % (len(tp), len(gfx) // 2))
    large = sum(1 for i in range(0, len(tp), 4) if tp[i + 1])
    if large != 31:
        raise SystemExit("%d large followers by tp_param, and HeartGold has 31"
                         % large)
    fx = pkg / ".cooked/narc/data/mmodel/fldeff.narc"
    for member, magic4 in zip(ball, (b"BMD0", b"BMD0", b"BTA0")):
        got = (fx / str(member)).read_bytes()[:4]
        if got != magic4:
            raise SystemExit("ball effect member %d is %r, not %r"
                             % (member, got, magic4))

    seen = {}
    diglett = 0
    for v in modes:
        if v == 0xFF:
            continue
        if v & 0x04:
            diglett += 1
        seen[v & 0x03] = seen.get(v & 0x03, 0) + 1
    if 3 in seen:
        raise SystemExit("a follow mode of 3, and the field is two bits of"
                         " three values")
    if not (seen.get(0) and seen.get(1) and seen.get(2)):
        raise SystemExit("the follow modes are %r and all three should occur"
                         % seen)
    if diglett != 11:
        raise SystemExit("%d maps turn a Diglett away and the Bell Tower is 11"
                         % diglett)
    print("%d members, %d gfx rows, %d carried, sequences %s and %s;"
          " talk v%d, %d tp_param bytes (%d large), follow modes %r, %d Bell Tower"
          % (len(members), len(gfx), len(carried), seq[0][0], seq[1][0],
             version, len(tp), large, seen, diglett))
else:
    print("%d members, %d gfx rows, %d carried, sequences %s and %s"
          % (len(members), len(gfx), len(carried), seq[0][0], seq[1][0]))
PY
    then
        ok "every planted member is the cartridge's own bytes ($(cat "$tmp/folback.log"))"
    else
        bad "every planted member is the cartridge's own bytes"
        tail -2 "$tmp/folback.log" | sed 's/^/       /'
    fi

    # And the whole thing loads. Nearly two thousand members and 1,132 cooked
    # rows is a bigger package than anything else in this repo hands the port:
    # 568 of art, and the rest the talk's five tables and its message bank.
    # The number is counted off the package rather than typed here, because it
    # moves whenever the fill grows and a stale constant would only ever fail
    # for the wrong reason.
    if [ ! -x "$FUSED" ] || [ ! -f "$ROM" ] \
            || [ ! -f "$ENGINE/pc/replays/lab-settle.txt" ]; then
        echo "  SKIP (no fused build, Platinum image or settle replay to load it with)"
    else
        printf 'name SHORT\n' > "$tmp/fol.lab"
        members=$(find "$tmp/followers/.cooked/narc" -type f | wc -l)
        rc=0
        # Two of them, one from each band: a sequence is only built when a row
        # asks for it, so a lone normal follower would leave the shiny one
        # untouched and this would be measuring nothing.
        env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LABELS=0 \
            OPENMMO_FAKE_CROWD=2 OPENMMO_FAKE_GFX=1024,1590 \
            PC_MODS_DIR="$tmp" PC_MODS=followers \
            PC_ROM="$ROM" PC_SAVE="$tmp/fol.sav" PC_LAB="$tmp/fol.lab" \
            PC_LAB_AT=1800 PC_FRAMES=1800 PC_PACE=0 \
            PC_INPUT="$ENGINE/pc/replays/lab-settle.txt" \
            "$FUSED" > "$tmp/folboot.log" 2>&1 || rc=$?
        if [ "$rc" -ne 0 ]; then
            bad "the whole filled package loads (exit $rc)"
            tail -2 "$tmp/folboot.log" | sed 's/^/       /'
        elif grep -q "$members members" "$tmp/folboot.log" \
                && grep -q 'cooked billboard sequence 64' "$tmp/folboot.log" \
                && grep -q 'cooked billboard sequence 65' "$tmp/folboot.log"
        then
            ok "the fused build loads all $members members and both sequences"
        else
            bad "the fused build loads all $members members and both sequences"
            grep -h 'modfs:\|cooked billboard' "$tmp/folboot.log" | head -3 \
                | sed 's/^/       /'
        fi
    fi
fi

echo "a player's cartridge fills it, and the wrong one does not:"

filled=
if [ ! -f "$SRC" ]; then
    echo "  SKIP (no source cartridge at $SRC; set OPENMMO_IMPORT_ROM)"
elif porter_missing; then
    :
else
    if "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$SRC" "$tmp/imports" \
            > "$tmp/fill.log" 2>&1 \
            && grep -q "modport: $lines line(s) from" "$tmp/fill.log"; then
        ok "the driver ports all $lines line(s) into a copy"
        filled=1
    else
        bad "the driver ports all $lines line(s) into a copy"
        tail -3 "$tmp/fill.log"
    fi

    # The image the client itself boots is a Gen 4 image with the same sprite
    # archive, so nothing but the registry stops it answering these lines. It
    # is refused as the slot it fills, the host image, rather than as a
    # string that did not match, and the sentence carries the row's own note.
    if [ -f "$ROM" ]; then
        if "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$ROM" "$tmp/wrong" \
                > "$tmp/wrong.log" 2>&1; then
            bad "a cartridge the recipe did not ask for is refused"
        elif grep -q "does not fill a package" "$tmp/wrong.log" \
                && grep -q "Platinum (CPUE)" "$tmp/wrong.log" \
                && grep -q "'host'" "$tmp/wrong.log" \
                && [ ! -d "$tmp/wrong" ]; then
            ok "a cartridge the recipe did not ask for is refused, writing nothing"
        else
            bad "a cartridge the recipe did not ask for is refused, writing nothing"
            tail -2 "$tmp/wrong.log"
        fi
    fi

    # Every image the registry turns away says which slot it is and what that
    # slot was measured to be. A refusal that named only a category would be a
    # verdict; these are readings, and the client's own table says the same.
    if [ -x "$CLIENT" ]; then
        agree=1
        why=
        for code in CPUE ADAE IRBO; do
            slot=$("$CLIENT" cartridges "$code" 2>/dev/null | head -1 | cut -d' ' -f2)
            # An empty slot searched for is `grep -q ""`, which matches any file
            # there is: a client that printed nothing at all would agree with the
            # registry about every code. And the slot has to be the one on that
            # code's row rather than a word occurring somewhere in the file,
            # `platinum` appears on the Pearl row's prose too.
            if [ -z "$slot" ]; then
                agree=0
                why="$why
       $code: the client named no slot"
            elif ! grep -q "^$code  *$slot\([ 	]\|$\)" "$ROOT/CARTRIDGES"; then
                agree=0
                why="$why
       $code: the client says $slot, its registry row does not"
            fi
        done
        if [ "$agree" = 1 ]; then
            ok "the client and the registry name the same slot for a code"
        else
            bad "the client and the registry name the same slot for a code"
            printf '%s\n' "$why" | sed '/^$/d'
        fi
    else
        echo "  SKIP (no client binary to compare the registry against)"
    fi
fi

# Music is refused on a measurement, and the numbers in the refusal are the
# cartridges', not this file's memory of them: both SDATs are read here and the
# counts the driver quotes have to be the ones that come back. A refusal
# carrying a number nobody re-derives is how a measurement becomes folklore.
if porter_missing; then
    :
elif [ ! -f "$ROM" ] || [ ! -f "$SRC" ]; then
    echo "  SKIP (need both cartridges to re-derive the SDAT counts)"
else
    mkdir -p "$tmp/music"
    cp "$PKG/mod.toml" "$tmp/music/mod.toml"
    echo "IPKE  music  route_29  route_201" > "$tmp/music/port.recipe"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/music" "$SRC" \
            "$tmp/musicout" > "$tmp/music.log" 2>&1; then
        bad "a music line is turned down by name"
    elif grep -q "an SDAT is one container" "$tmp/music.log" \
            && [ ! -d "$tmp/musicout" ]; then
        ok "a music line is turned down by name, writing nothing"
    else
        bad "a music line is turned down by name, writing nothing"
        tail -2 "$tmp/music.log"
    fi

    # The SDAT reader is the porter's own NitroRom, imported rather than
    # rewritten, a second reader of an NDS image is what §73 refuses, and it
    # is also how a check comes to disagree with the tool it checks.
    counts=$(OPENMMO_PORTER_DIR="$ENGINE/pc" python3 - "$ROM" "$SRC" <<'PYSDAT'
import struct, sys
sys.path.insert(0, __import__("os").environ["OPENMMO_PORTER_DIR"])
from modport import NitroRom
from pathlib import Path
for path, name in ((sys.argv[1], "data/sound/pl_sound_data.sdat"),
                   (sys.argv[2], "data/sound/gs_sound_data.sdat")):
    blob = NitroRom(Path(path)).file_bytes(name)
    n = struct.unpack_from("<H", blob, 0x0E)[0]
    blocks = [struct.unpack_from("<II", blob, 0x10 + i * 8) for i in range(n)]
    off = next(o for o, s in blocks if blob[o:o + 4] == b"INFO")
    recs = struct.unpack_from("<8I", blob, off + 8)
    print(" ".join(str(struct.unpack_from("<I", blob, off + recs[i])[0])
                   for i in (0, 2, 3)))
PYSDAT
)
    # Command substitution strips the trailing whitespace `tr` leaves, so the
    # pattern has none either.
    want=$(printf '%s' "$counts" | tr '\n' ' ')
    if [ "$want" = "2133 771 771 2379 778 778" ] \
            && grep -q "2133 sequences / 771 banks / 771 wave archives here, 2379 / 778 / 778 there" \
                    "$tmp/music.log"; then
        ok "and its numbers are the two cartridges', re-derived here"
    else
        bad "and its numbers are the two cartridges', re-derived here"
        echo "       read back: $want"
    fi
fi

# One package, one cartridge, the shape, not just the default. A recipe
# naming two codes is refused before either is opened, because a package filled
# by two images cannot answer "which one filled this" and one image cannot
# answer both halves anyway.
if [ -z "$filled" ]; then
    echo "  SKIP (no cartridge to check the two-cartridge shape against)"
else
    # The three candidates that did NOT become kinds are refused by name, with
    # the measurement in the sentence. Unrecognised would be indistinguishable
    # from a gap (§83), which is the whole reason each of these is spelled out.
    for k in mmodel build_model cry; do
        mkdir -p "$tmp/ref$$"
        cp "$PKG/mod.toml" "$tmp/ref$$/mod.toml"
        echo "IPKE  $k  a  b" > "$tmp/ref$$/port.recipe"
        if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/ref$$" "$SRC" \
                "$tmp/refout$$" > "$tmp/ref$$.log" 2>&1; then
            bad "'$k' is refused by name"
        elif grep -q "'$k' is refused rather than unported" "$tmp/ref$$.log" \
                && grep -q "mmo/ASSETS.md" "$tmp/ref$$.log" \
                && [ ! -d "$tmp/refout$$" ]; then
            ok "'$k' is refused by name, with the measurement and where to read it"
        else
            bad "'$k' is refused by name, with the measurement and where to read it"
            tail -2 "$tmp/ref$$.log"
        fi
        rm -rf "$tmp/ref$$" "$tmp/refout$$"
    done

    mkdir -p "$tmp/twocart"
    cp "$PKG/mod.toml" "$tmp/twocart/mod.toml"
    printf 'IPKE  pokemon  chikorita  turtwig\nIPGE  pokemon  totodile  piplup\n' \
        > "$tmp/twocart/port.recipe"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/twocart" "$SRC" \
            "$tmp/twocartout" > "$tmp/twocart.log" 2>&1; then
        bad "a recipe naming two cartridges is refused"
    elif grep -q "names IPKE and IPGE" "$tmp/twocart.log" \
            && grep -q "its own package" "$tmp/twocart.log" \
            && [ ! -d "$tmp/twocartout" ]; then
        ok "a recipe naming two cartridges is refused, and says what to do"
    else
        bad "a recipe naming two cartridges is refused, and says what to do"
        tail -2 "$tmp/twocart.log"
    fi
fi

# WHAT FILLED IT. A directory of cartridge bytes with no record of which image
# answered for them cannot be re-filled reproducibly, and the recipe cannot say
# it either: IPKE and IPKD are the same lines and different art.
if [ -z "$filled" ]; then
    echo "  SKIP (nothing was filled)"
else
    if grep -q "^fill IPKE Heart Gold (english)" "$tmp/imports/port.log"; then
        ok "the fill opens by naming the cartridge, its slot and its language"
    else
        bad "the fill opens by naming the cartridge, its slot and its language"
        head -1 "$tmp/imports/port.log"
    fi

    # A package another cartridge already filled. There is one read cartridge
    # on this machine, so the earlier fill is written rather than performed,
    # what is under test is the driver's answer to the record, not the record.
    mkdir -p "$tmp/twice"
    cp "$PKG/mod.toml" "$PKG/port.recipe" "$tmp/twice/"
    echo "fill IPGE SoulSilver (english) POKEMON SS" > "$tmp/twice/port.log"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$SRC" "$tmp/twice" \
            > "$tmp/twice.log" 2>&1; then
        bad "a package already filled from another cartridge is refused"
    elif grep -q "already filled from IPGE" "$tmp/twice.log"; then
        ok "a package already filled from another cartridge is refused by code"
    else
        bad "a package already filled from another cartridge is refused by code"
        tail -2 "$tmp/twice.log"
    fi

    if OPENMMO_REFILL=1 "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$SRC" \
            "$tmp/twice" > "$tmp/refill.log" 2>&1 \
            && grep -q "was filled from IPGE and is being re-filled" "$tmp/refill.log"; then
        ok "and OPENMMO_REFILL=1 goes through, saying what it replaced"
    else
        bad "and OPENMMO_REFILL=1 goes through, saying what it replaced"
        tail -2 "$tmp/refill.log"
    fi
fi

# WHAT AN UNFILLED PACKAGE COSTS, said where it costs it. The tracked package
# is the unfilled case on every machine whose owner has no cartridge, and it
# looks exactly like a working one from inside the game: an unclaimed member
# falls through to the player's own image. So the client is asked, and its
# answer has to name the cartridge rather than report a count.
if [ ! -x "$CLIENT" ]; then
    echo "  SKIP (no client binary to ask what a package costs)"
else
    # The unfilled case is a copy with no fill log, not the working tree's
    # package: on a machine whose owner has the cartridge that one is filled,
    # which is the normal state and not a reason for this check to fail.
    mkdir -p "$tmp/unfilled"
    cp "$PKG/mod.toml" "$PKG/port.recipe" "$tmp/unfilled/"
    # `set -e` is on and this command exits non-zero on purpose, so the
    # status is taken through `||` rather than by reading $? after it.
    rc=0
    out=$("$CLIENT" imports "$tmp/unfilled" 2>&1) || rc=$?
    if [ "$rc" -eq 1 ] \
            && echo "$out" | grep -q "unfilled" \
            && echo "$out" | grep -q "Heart Gold" \
            && echo "$out" | grep -q "item icon"; then
        ok "an unfilled package names the cartridge and what it would bring"
    else
        bad "an unfilled package names the cartridge and what it would bring"
        echo "$out" | head -2
    fi
    if [ -n "$filled" ]; then
        rc=0
        out=$("$CLIENT" imports "$tmp/imports" 2>&1) || rc=$?
        if [ "$rc" -eq 0 ] && echo "$out" | grep -q "filled by IPKE"; then
            ok "and a filled one says which cartridge answered, costing nothing"
        else
            bad "and a filled one says which cartridge answered, costing nothing"
            echo "$out" | head -2
        fi
    fi
fi

# The folder, rather than a path on a make line. Every file in it gets a line,
# including the ones that were not cartridges, and a renamed image is still
# identified, by the header, through the porter, because a second reader of an
# NDS header is what SETTLED §73 refuses and also how two answers about one
# cartridge start.
if [ ! -f "$SRC" ]; then
    echo "  SKIP (no source cartridge to scan for)"
elif porter_missing; then
    :
else
    scan="$tmp/folder"
    mkdir -p "$scan"
    cp "$SRC" "$scan/renamed-by-the-player.nds"
    echo "not a cartridge" > "$scan/notes.txt"
    : > "$scan/backup.zip"
    if sh "$ROOT/tools/scan_cartridges.sh" "$scan" --quiet 2>"$tmp/scan.err"; then
        m="$scan/cartridges.found"
        if grep -q "^read IPKE renamed-by-the-player.nds" "$m"; then
            ok "a renamed image is identified by its header, not its name"
        else
            bad "a renamed image is identified by its header, not its name"
            grep -v "^#" "$m" | head -3
        fi
        if grep -q "^not-a-cartridge notes.txt" "$m" \
                && grep -q "^archive backup.zip" "$m"; then
            ok "and the files that were not cartridges are in the record too"
        else
            bad "and the files that were not cartridges are in the record too"
            grep -v "^#" "$m" | head -3
        fi
        if [ -x "$CLIENT" ]; then
            rc=0
            out=$("$CLIENT" imports "$tmp/unfilled" --found "$m" 2>&1) || rc=$?
            if [ "$rc" -eq 1 ] && echo "$out" | grep -q "renamed-by-the-player.nds"; then
                ok "an unfilled package points at the image already in the folder"
            else
                bad "an unfilled package points at the image already in the folder"
                echo "$out" | head -2
            fi
        fi
        if cmp -s "$SRC" "$scan/renamed-by-the-player.nds"; then
            ok "the scan read the images where they lay and copied nothing"
        else
            bad "the scan read the images where they lay and copied nothing"
        fi
    else
        bad "the cartridge folder scans"
        tail -2 "$tmp/scan.err"
    fi
fi

# A SECOND GEN 4 CARTRIDGE, whose sheets scramble the other way. Diamond used
# to be refused on the porter's own warning: same container, same depth, same
# dimensions, and noise on the screen.
DIA=${OPENMMO_DIAMOND_ROM:-$(CDPATH= cd -- "$ROOT/.." && pwd)/roms/pokediamond.nds}
if [ ! -f "$DIA" ]; then
    echo "  SKIP (no Diamond cartridge at $DIA; set OPENMMO_DIAMOND_ROM)"
elif porter_missing; then
    :
else
    mkdir -p "$tmp/dia"
    printf 'id = "dia"\nname = "dia"\nversion = "1.0.0"\n' > "$tmp/dia/mod.toml"
    echo "ADAE  pokemon  bulbasaur  bulbasaur" > "$tmp/dia/port.recipe"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/dia" "$DIA" "$tmp/diaout" \
            > "$tmp/dia.log" 2>&1; then
        ok "a Diamond cartridge fills a package, where it used to be refused"
    else
        bad "a Diamond cartridge fills a package, where it used to be refused"
        tail -2 "$tmp/dia.log"
    fi
    sheets="$tmp/diaout/narc/$NARC"
    wrong=0
    right=0
    for n in 6 7 8 9; do
        [ -f "$sheets/$n" ] || { wrong=$((wrong + 1)); continue; }
        python3 "$ROOT/tools/rescramble.py" --check "$sheets/$n" 2 \
            > /dev/null 2>&1 && right=$((right + 1))
        python3 "$ROOT/tools/rescramble.py" --check "$sheets/$n" 1 \
            > /dev/null 2>&1 && wrong=$((wrong + 1))
    done
    if [ "$right" = 4 ] && [ "$wrong" = 0 ]; then
        ok "all four sheets read as this game's direction and not Diamond's"
    else
        bad "all four sheets read as this game's direction and not Diamond's"
        echo "       $right of 4 read as ours, $wrong still read as Diamond's"
    fi
    # The palettes are not scrambled and must come across untouched.
    if python3 "$PORTER" --rom "$DIA" --out "$tmp/diavan" \
            --narc poketool/pokegra/pokegra.narc --member 10 \
            > /dev/null 2>&1 \
            && cmp -s "$tmp/diavan/narc/poketool/pokegra/pokegra.narc/10" \
                      "$sheets/10"; then
        ok "and the palette crossed byte for byte, because it is not scrambled"
    else
        bad "and the palette crossed byte for byte, because it is not scrambled"
    fi
fi

# --- the four player looks -----------------------------------------------
echo "the four player looks (portlooks.py against lookcompose.c):"
if [ ! -f "$SRC4" ]; then
    echo "  SKIP (no Heart Gold or Soul Silver cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no Platinum at $ROM)"
elif python3 "$ROOT/tools/portlooks.py" --hg "$SRC4" --bw "$SRC5" --pt "$ROM" \
        --pkg "$tmp/looks" > "$tmp/looks.log" 2>&1; then
    ok "the porter fills a package ($(sed -n '1s/portlooks: //p' "$tmp/looks.log"))"
    n=$(find "$tmp/looks/.cooked/narc" -type f | wc -l)
    if [ "$n" -eq 71 ]; then
        ok "thirty sheets, four fronts, four backs and the class-name bank: 71 members"
    else
        bad "thirty sheets, four fronts, four backs and the class-name bank: 71 members (got $n)"
    fi
    if [ "$(wc -l < "$tmp/looks/.cooked/generated/player_looks.txt")" -eq 4 ] \
       && grep -q '^0 105 11 0 ' "$tmp/looks/.cooked/generated/player_looks.txt" \
       && grep -q '^3 108 14 -1 ' "$tmp/looks/.cooked/generated/player_looks.txt" \
       && grep -q '^768 470 -1 -1 0$' "$tmp/looks/.cooked/generated/billboard_gfx.txt" \
       && grep -q '^816 494 -1 -1 97$' "$tmp/looks/.cooked/generated/billboard_gfx.txt"; then
        ok "the looks name their classes and backs, and every sheet its Platinum like"
    else
        bad "the looks name their classes and backs, and every sheet its Platinum like"
    fi
    if [ ! -x "$BUILD/openmmo-launch" ]; then
        echo "  SKIP (no launcher built; make -C mmo launcher)"
    elif "$BUILD/openmmo-launch" --compose-looks "$SRC4" "$SRC5" "$ROM" \
            "$tmp/clooks" > "$tmp/clooks.log" 2>&1; then
        if diff -r "$tmp/looks/.cooked" "$tmp/clooks/.cooked" > "$tmp/lookdiff.log" 2>&1 \
           && cmp -s "$tmp/looks/mod.toml" "$tmp/clooks/mod.toml" \
           && cmp -s "$tmp/looks/composed.txt" "$tmp/clooks/composed.txt"; then
            ok "the launcher's fill is the porter's, byte for byte"
        else
            bad "the launcher's fill is the porter's, byte for byte"
            head -3 "$tmp/lookdiff.log" | sed 's/^/       /'
        fi
        # A refill at other bases leaves nothing of the first: the members are
        # written by index, so this is what makes a moved base true. The class
        # base stays at 105 here because the name bank it grows has to be
        # exactly that long, and only a loaded world package makes it longer.
        if "$BUILD/openmmo-launch" --compose-looks "$SRC4" "$SRC5" "$ROM" \
                "$tmp/clooks" 500 105 20 > "$tmp/clooks2.log" 2>&1 \
           && [ "$(find "$tmp/clooks/.cooked/narc" -type f | wc -l)" -eq 71 ] \
           && [ -f "$tmp/clooks/.cooked/narc/data/mmodel/mmodel.narc/500" ] \
           && [ ! -f "$tmp/clooks/.cooked/narc/data/mmodel/mmodel.narc/470" ] \
           && [ -f "$tmp/clooks/.cooked/narc/poketool/trgra/trbgra.narc/100" ] \
           && [ ! -f "$tmp/clooks/.cooked/narc/poketool/trgra/trbgra.narc/55" ] \
           && grep -q '^v1 500 105 20$' "$tmp/clooks/composed.txt"; then
            ok "a refill at new bases replaces the package rather than adding a band"
        else
            bad "a refill at new bases replaces the package rather than adding a band"
            tail -2 "$tmp/clooks2.log" | sed 's/^/       /'
        fi
    else
        bad "the launcher's own look fill runs"
        tail -2 "$tmp/clooks.log" | sed 's/^/       /'
    fi
    # And the fused client claims the package whole: thirty sheets past the
    # image's 470, four classes past 105, four backs past 11, one bank.
    if [ ! -x "$FUSED" ]; then
        echo "  SKIP (no fused client)"
    elif env PC_ROM="$ROM" PC_SAVE="$tmp/looks.sav" PC_FRAMES=120 PC_PACE=0 \
            PC_MODS_DIR="$tmp" PC_MODS=looks "$FUSED" > "$tmp/looksboot.log" 2>&1 \
         && grep -q 'modfs: 0 files, 71 members, order=\[looks\]' "$tmp/looksboot.log"; then
        ok "the fused client claims all 71 members with no hole"
    else
        bad "the fused client claims all 71 members with no hole"
        grep -E 'modfs|hole|die' "$tmp/looksboot.log" | head -3 | sed 's/^/       /'
    fi
else
    bad "the porter fills a package"
    tail -2 "$tmp/looks.log" | sed 's/^/       /'
fi

# --- the driver's own door ------------------------------------------------

echo "the driver refuses before it writes, and writes nothing when it refuses:"

# A decomp checkout is where the server's tables come from. It is not a
# cartridge and it is not a client source, so it is turned down by name,
# "no ROM at .../pokeheartgold" reads like a path typo somebody could fix.
tree=$("$ROOT/tools/decomp_dir.sh" pokeheartgold 2>/dev/null \
    || echo "")
[ -d "$tree" ] || tree="$ROOT/mods"
if porter_missing; then
    :
elif "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$tree" "$tmp/tree" \
        > "$tmp/tree.log" 2>&1; then
    bad "a source tree handed in place of a cartridge is refused"
elif grep -q 'is not a source for this client' "$tmp/tree.log" \
        && [ ! -d "$tmp/tree" ]; then
    ok "a decompilation checkout is refused by name, writing nothing"
else
    bad "a decompilation checkout is refused by name, writing nothing"
    tail -2 "$tmp/tree.log"
fi

# An item_icon line is two lookups into a committed table, so a line naming an
# item a side does not have, or a destination whose palette other items share,
# is answerable before the cartridge is opened, and both refuse by name.
if [ -z "$filled" ]; then
    echo "  SKIP (no cartridge to check the icon refusals against)"
else
    # A trainer line with a class one side does not have, and one whose class
    # is past the cartridge's own archive: both refuse by name, the second on
    # the porter's own bound.
    for case in "trainer nosuchclass youngster:has no trainer class called" \
                "trainer phone_mom youngster:archive stops before it"; do
        set -- $case
        mkdir -p "$tmp/tr$$"
        cp "$PKG/mod.toml" "$tmp/tr$$/mod.toml"
        echo "IPKE  $1  $2  ${3%%:*}" > "$tmp/tr$$/port.recipe"
        want=${case#*:}
        if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/tr$$" "$SRC" \
                "$tmp/trout$$" > "$tmp/tr$$.log" 2>&1; then
            bad "a trainer line naming '$2' is refused"
        elif grep -q "$want" "$tmp/tr$$.log" && [ ! -d "$tmp/trout$$" ]; then
            ok "'$2' is refused by name, writing nothing"
        else
            bad "'$2' is refused by name, writing nothing"
            tail -2 "$tmp/tr$$.log"
        fi
        rm -rf "$tmp/tr$$" "$tmp/trout$$"
    done

    for case in "nosuchitem cherish_ball:has no item called" \
                "red_apricorn timer_ball:items share" \
                "red_apricorn red_apricorn:one of Heart Gold's item names"; do
        line=${case%%:*}; want=${case#*:}
        mkdir -p "$tmp/icon$$"
        cp "$PKG/mod.toml" "$tmp/icon$$/mod.toml"
        echo "IPKE  item_icon  $line" > "$tmp/icon$$/port.recipe"
        if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/icon$$" "$SRC" \
                "$tmp/iconout$$" > "$tmp/icon$$.log" 2>&1; then
            bad "an item_icon line naming '$line' is refused"
        elif grep -q "$want" "$tmp/icon$$.log" && [ ! -d "$tmp/iconout$$" ]; then
            ok "'$line' is refused by name, writing nothing"
        else
            bad "'$line' is refused by name, writing nothing"
            tail -2 "$tmp/icon$$.log"
        fi
        rm -rf "$tmp/icon$$" "$tmp/iconout$$"
    done
fi

if [ -z "$filled" ]; then
    echo "  SKIP (no cartridge to fail a fill against)"
else
    mkdir -p "$tmp/half"
    cp "$PKG/mod.toml" "$tmp/half/mod.toml"
    # Line one is the recipe's own first line and ports; line two names nothing.
    awk '!/^[[:space:]]*(#|$)/' "$RECIPE" | head -1 > "$tmp/half/port.recipe"
    awk '!/^[[:space:]]*(#|$)/ {print $1, $2, "nosuchmon", $4; exit}' \
        "$RECIPE" >> "$tmp/half/port.recipe"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/half" "$SRC" "$tmp/halfout" \
            > "$tmp/half.log" 2>&1; then
        bad "a recipe that fails on its second line publishes nothing"
    elif grep -q 'nothing was written' "$tmp/half.log" \
            && [ ! -d "$tmp/halfout/narc" ]; then
        ok "a recipe that fails on its second line publishes nothing,"
        ok "  not the first line's six members"
    else
        bad "a recipe that fails on its second line publishes nothing"
        tail -2 "$tmp/half.log"
        find "$tmp/halfout" -type f 2>/dev/null | head -3
    fi

    # Art the porter copies and then warns about is art the engine's sprite
    # path does not decode, Diamond's, which no image here is. The rule is
    # the driver's own, so it is driven through the driver: a porter that says
    # anything on stderr after a successful line is a refusal.
    cat > "$tmp/warnport" <<'WRAP'
#!/bin/sh
python3 "$@"; rc=$?
case "$*" in
*--pokemon*) echo "port: warning: 6448-byte NCGR does not decode through" \
                  "Platinum's sprite path" >&2 ;;
esac
exit $rc
WRAP
    chmod +x "$tmp/warnport"
    if PYTHON="$tmp/warnport" "$ROOT/tools/modport.sh" "$ENGINE" "$PKG" "$SRC" \
            "$tmp/warned" > "$tmp/warned.log" 2>&1; then
        bad "art the porter says will not draw is refused, not published"
    elif grep -q 'said it will not draw' "$tmp/warned.log" \
            && [ ! -d "$tmp/warned/narc" ]; then
        ok "art the porter says will not draw is refused, not published"
    else
        bad "art the porter says will not draw is refused, not published"
        tail -2 "$tmp/warned.log"
    fi
fi

echo "each line lands on the member our own table draws that species from:"

# One representative line drives the boot: they differ only in which numbers
# they name, and a boot is measured per-probe rather than per-package.
probe_member=
probe_species=
if [ -z "$filled" ]; then
    echo "  SKIP (nothing was filled)"
elif [ ! -x "$CLIENT" ]; then
    echo "  SKIP (no client binary to compute the table)"
else
    while read -r kind rest; do
        [ "$kind" = pokemon ] || continue
        # port.log: pokemon IPKE:name src (N) -> dst (M) <narc>/<lo>..<hi>
        dst=$(echo "$rest" | sed -n 's/.*-> \([a-z0-9_]*\) (\([0-9]*\)).*/\1 \2/p')
        set -- $dst
        name=${1:-}; num=${2:-}
        span=$(echo "$rest" | sed -n 's|.*/\([0-9]*\)\.\.\([0-9]*\)$|\1 \2|p')
        set -- $span
        lo=${1:-}; hi=${2:-}
        if [ -z "$name" ] || [ -z "$num" ] || [ -z "$lo" ]; then
            bad "the port log names a destination species and a member span"
            continue
        fi
        # The client's own arithmetic, with no engine present: species -> the
        # character and palette members it would read.
        row=$("$CLIENT" sprite --species "$num" 2>/dev/null | head -1)
        char=$(echo "$row" | sed -n 's/.*character \([0-9]*\) palette \([0-9]*\).*/\1/p')
        pal=$(echo "$row" | sed -n 's/.*character \([0-9]*\) palette \([0-9]*\).*/\2/p')
        if [ -z "$char" ] || [ -z "$pal" ]; then
            bad "the client resolves $name ($num) to a sprite member"
            continue
        fi
        if [ "$char" -ge "$lo" ] && [ "$char" -le "$hi" ] \
                && [ "$pal" -ge "$lo" ] && [ "$pal" -le "$hi" ] \
                && [ $((hi - lo)) -eq 5 ]; then
            ok "$name ($num) draws from $char/$pal, inside the ported $lo..$hi"
        else
            bad "$name ($num) draws from $char/$pal, inside the ported $lo..$hi"
        fi
        n=$lo
        missing=
        while [ "$n" -le "$hi" ]; do
            [ -f "$tmp/imports/narc/$NARC/$n" ] || missing="$missing $n"
            n=$((n + 1))
        done
        if [ -z "$missing" ]; then
            ok "all six members $lo..$hi were written"
        else
            bad "all six members $lo..$hi were written (missing:$missing)"
        fi
        probe_member=$char
        probe_species=$name
    done < "$tmp/imports/port.log"

    # The trainer line, likewise. Five members and the same placement problem,
    # so the same evidence: the run landed where the destination class draws
    # from, and the table says so rather than this file remembering.
    tr_line=$(sed -n 's/^trainer [^ ]* (\([^)]*\)) -> narc\/\([^ ]*\)\/\([0-9]*\)\.\.\([0-9]*\) (\(.*\))$/\1 \2 \3 \4 \5/p' \
              "$tmp/imports/port.log" | head -1)
    if [ -n "$tr_line" ]; then
        set -- $tr_line
        tr_src=$1; tr_narc=$2; tr_lo=$3; tr_hi=$4; tr_dst=$5
        missing=
        n=$tr_lo
        while [ "$n" -le "$tr_hi" ]; do
            [ -f "$tmp/imports/narc/$tr_narc/$n" ] || missing="$missing $n"
            n=$((n + 1))
        done
        if [ -z "$missing" ] && [ $((tr_hi - tr_lo)) -eq 4 ]; then
            ok "$tr_src's five members landed on $tr_dst's $tr_lo..$tr_hi"
        else
            bad "$tr_src's five members landed on $tr_dst's $tr_lo..$tr_hi (missing:$missing)"
            tr_lo=
        fi
        exp=$(awk -v c="$tr_dst" '$1 == "pl" && $2 == c { print $3 }' \
              "$ROOT/TRAINER_GFX")
        if [ "$exp" = "$tr_lo" ]; then
            ok "and that is where mmo/TRAINER_GFX says $tr_dst starts"
        else
            bad "and that is where mmo/TRAINER_GFX says $tr_dst starts"
            echo "       table says: ${exp:-nothing}"
        fi
    fi

    # The item_icon lines, read out of the same log. An icon is two members
    # and the porter writes a raw member at the index it read, so the log line
    # is also the only record that the driver moved it onto the destination's
    # slot, if the move did not happen the file below is simply not there.
    icon_line=$(sed -n 's/^item_icon [^ ]* (\([^)]*\)) -> narc\/\([^ ]*\)\/\([0-9]*\),\([0-9]*\) (\(.*\))$/\1 \2 \3 \4 \5/p' \
                "$tmp/imports/port.log" | head -1)
    if [ -n "$icon_line" ]; then
        set -- $icon_line
        icon_src=$1; icon_narc=$2; icon_ncgr=$3; icon_nclr=$4; icon_dst=$5
        if [ -f "$tmp/imports/narc/$icon_narc/$icon_ncgr" ] \
                && [ -f "$tmp/imports/narc/$icon_narc/$icon_nclr" ]; then
            ok "$icon_src's two members landed on $icon_dst's $icon_ncgr,$icon_nclr"
        else
            bad "$icon_src's two members landed on $icon_dst's $icon_ncgr,$icon_nclr"
            icon_ncgr=
        fi
        # A line that lands on the source numbers would leave the destination
        # untouched and still look filled, so insist the two differ.
        exp=$(awk -v i="$icon_dst" '$1 == "pl" && $2 == i { print $3, $4 }' \
              "$ROOT/ITEM_ICONS")
        if [ "$exp" = "$icon_ncgr $icon_nclr" ]; then
            ok "and they are the members mmo/ITEM_ICONS says $icon_dst draws from"
        else
            bad "and they are the members mmo/ITEM_ICONS says $icon_dst draws from"
            echo "       table says: ${exp:-nothing}"
        fi
    fi
fi

echo "the fused build serves the ported art over the player's image:"

boot() { # LOG VAR=VALUE ...
    _log=$1; shift
    env -u PC_MODS -u PC_MODS_DIR -u PC_MODFS \
        PC_ROM="$ROM" PC_SAVE=none PC_FRAMES=60 PC_PACE=0 \
        "$@" "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
}

if [ -z "$probe_member" ]; then
    echo "  SKIP (nothing was filled)"
elif [ ! -x "$FUSED" ]; then
    echo "  SKIP (no fused build: run \`make -C mmo fused\`)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no ROM at $ROM)"
else
    rom_before=$(stamp "$ROM")
    member="$tmp/imports/narc/$NARC/$probe_member"

    rc=$(boot "$tmp/on.log" PC_MODS_DIR="$tmp" PC_MODS=imports \
              PC_MODFS_PROBE_COUNT="$NARC/$probe_member")
    got=$(sed -n "s|.*probe-count $NARC \([0-9]*\) $probe_member \([0-9]*\) \(.*\)|\1 \2 \3|p" \
          "$tmp/on.log")
    set -- ${got:-}
    count=${1:-}; size=${2:-}; hex=${3:-}
    if [ "$rc" -ne 0 ] || [ -z "$count" ]; then
        bad "the ported member reads back through the game's NARC path"
        tail -3 "$tmp/on.log"
    elif [ "$size" = "$(wc -c < "$member" | tr -d ' ')" ] \
            && [ "$hex" = "$(xxd -p -c 100000 "$member" | tr -d '\n')" ]; then
        ok "$probe_species's member $probe_member is the package's $size bytes"
    else
        bad "$probe_species's member $probe_member is the package's bytes"
        echo "       served $size bytes"
    fi
    if [ "${count:-0}" = 2964 ]; then
        ok "the archive is still 2964 members, a replacement, not an append"
    else
        bad "the archive is still 2964 members (got ${count:-none})"
    fi

    rc=$(boot "$tmp/off.log" PC_MODFS_PROBE_COUNT="$NARC/$probe_member")
    if [ "$rc" -ne 0 ] \
            && grep -q "member not claimed: $NARC/$probe_member" "$tmp/off.log"; then
        ok "without the package the overlay claims nothing there"
    else
        bad "without the package the overlay claims nothing there"
        tail -2 "$tmp/off.log"
    fi

    # The game's other read path, the one a sprite load actually takes: the
    # probe compares an object read against an index-pair read before printing.
    rc=$(boot "$tmp/narc.log" PC_MODS_DIR="$tmp" PC_MODS=imports \
              PC_MODFS_PROBE_NARC="$NARC/$probe_member")
    hex=$(sed -n "s|.*probe-narc $NARC/$probe_member [0-9]* ||p" "$tmp/narc.log")
    if [ "$rc" -eq 0 ] \
            && [ "$hex" = "$(xxd -p -c 100000 "$member" | tr -d '\n')" ]; then
        ok "NARC_ReadWholeMember agrees with the index-pair read on those bytes"
    else
        bad "NARC_ReadWholeMember agrees with the index-pair read on those bytes"
        tail -2 "$tmp/narc.log"
    fi

    # What the cartridge has at that member, taken with the same tool.
    if python3 "$PORTER" --rom "$ROM" --out "$tmp/vanilla" \
            --narc "$NARC" --member "$probe_member" > "$tmp/van.log" 2>&1; then
        van="$tmp/vanilla/narc/$NARC/$probe_member"
        if cmp -s "$van" "$member"; then
            bad "the ported art is not the cartridge's own"
        elif cmp -s -n 32 "$van" "$member"; then
            ok "same NCGR header as the member it covers, different pixels"
        else
            bad "same NCGR header as the member it covers, different pixels"
            xxd -l 32 "$van"; xxd -l 32 "$member"
        fi
    else
        bad "the cartridge's own member at $probe_member can be read back"
        tail -2 "$tmp/van.log"
    fi

    # The second kind, through the same door. This is 20.5's rule made
    # concrete for item_icon: the engine's own NARC path answers the ported
    # bytes at the member the destination item draws from, the archive is the
    # same size it was, and the header the icon loader reads is the shape of
    # the member it covers with different pixels underneath.
    if [ -z "${icon_ncgr:-}" ]; then
        echo "  SKIP (no item_icon line was filled)"
    else
        icon_file="$tmp/imports/narc/$icon_narc/$icon_ncgr"
        rc=$(boot "$tmp/icon.log" PC_MODS_DIR="$tmp" PC_MODS=imports \
                  PC_MODFS_PROBE_COUNT="$icon_narc/$icon_ncgr")
        got=$(sed -n "s|.*probe-count $icon_narc \([0-9]*\) $icon_ncgr \([0-9]*\) \(.*\)|\1 \2 \3|p" \
              "$tmp/icon.log")
        set -- ${got:-}
        icount=${1:-}; isize=${2:-}; ihex=${3:-}
        if [ "$rc" -ne 0 ] || [ -z "$icount" ]; then
            bad "the ported icon reads back through the game's NARC path"
            tail -3 "$tmp/icon.log"
        elif [ "$isize" = "$(wc -c < "$icon_file" | tr -d ' ')" ] \
                && [ "$ihex" = "$(xxd -p -c 100000 "$icon_file" | tr -d '\n')" ]; then
            ok "$icon_dst's member $icon_ncgr is the package's $isize bytes"
        else
            bad "$icon_dst's member $icon_ncgr is the package's bytes"
            echo "       served $isize bytes"
        fi
        if [ "${icount:-0}" = 711 ]; then
            ok "the icon archive is still 711 members, a replacement"
        else
            bad "the icon archive is still 711 members (got ${icount:-none})"
        fi

        if python3 "$PORTER" --rom "$ROM" --out "$tmp/vanicon" \
                --narc "$icon_narc" --member "$icon_ncgr" \
                > "$tmp/vanicon.log" 2>&1; then
            van="$tmp/vanicon/narc/$icon_narc/$icon_ncgr"
            if cmp -s "$van" "$icon_file"; then
                bad "the ported icon is not the cartridge's own"
            elif cmp -s -n 32 "$van" "$icon_file"; then
                ok "same NCGR header as the icon it covers, different pixels"
            else
                bad "same NCGR header as the icon it covers, different pixels"
                xxd -l 32 "$van"; xxd -l 32 "$icon_file"
            fi
        else
            bad "the cartridge's own icon at $icon_ncgr can be read back"
            tail -2 "$tmp/vanicon.log"
        fi
    fi

    # The third kind through the same door.
    if [ -z "${tr_lo:-}" ]; then
        echo "  SKIP (no trainer line was filled)"
    else
        tr_file="$tmp/imports/narc/$tr_narc/$tr_lo"
        rc=$(boot "$tmp/tr.log" PC_MODS_DIR="$tmp" PC_MODS=imports \
                  PC_MODFS_PROBE_COUNT="$tr_narc/$tr_lo")
        got=$(sed -n "s|.*probe-count $tr_narc \([0-9]*\) $tr_lo \([0-9]*\) \(.*\)|\1 \2 \3|p" \
              "$tmp/tr.log")
        set -- ${got:-}
        tcount=${1:-}; tsize=${2:-}; thex=${3:-}
        if [ "$rc" -ne 0 ] || [ -z "$tcount" ]; then
            bad "the ported trainer reads back through the game's NARC path"
            tail -3 "$tmp/tr.log"
        elif [ "$tsize" = "$(wc -c < "$tr_file" | tr -d ' ')" ] \
                && [ "$thex" = "$(xxd -p -c 100000 "$tr_file" | tr -d '\n')" ]; then
            ok "$tr_dst's member $tr_lo is the package's $tsize bytes"
        else
            bad "$tr_dst's member $tr_lo is the package's bytes"
            echo "       served $tsize bytes"
        fi
        if [ "${tcount:-0}" = 525 ]; then
            ok "the trainer archive is still 525 members, a replacement"
        else
            bad "the trainer archive is still 525 members (got ${tcount:-none})"
        fi
        if python3 "$PORTER" --rom "$ROM" --out "$tmp/vantr" \
                --narc "$tr_narc" --member "$tr_lo" \
                > "$tmp/vantr.log" 2>&1; then
            van="$tmp/vantr/narc/$tr_narc/$tr_lo"
            # NOT byte-identical headers, and that is the measurement rather
            # than a weaker check: a species sheet is 6448 bytes for everybody
            # and an item icon 560, but a trainer sheet is as big as the
            # trainer, Platinum's Youngster is 3248 bytes and HeartGold's
            # Falkner 9648. What has to match is what the loader reads: the
            # magic, the RAHC section, the tile dimensions and the depth. The
            # two length fields are expected to differ, and if they did not
            # this would be the same picture.
            fields() {
                xxd -p -s 0 -l 8 "$1" | tr -d '\n'
                xxd -p -s 16 -l 4 "$1" | tr -d '\n'
                xxd -p -s 24 -l 8 "$1" | tr -d '\n'
            }
            if cmp -s "$van" "$tr_file"; then
                bad "the ported trainer is not the cartridge's own"
            elif [ "$(fields "$van")" = "$(fields "$tr_file")" ] \
                    && [ "$(wc -c < "$van")" != "$(wc -c < "$tr_file")" ]; then
                ok "same NCGR magic, section and 4bpp dims as the trainer it covers"
                ok "  and a different sheet size, $(wc -c < "$van" | tr -d ' ') bytes covered by $(wc -c < "$tr_file" | tr -d ' ')"
            else
                bad "same NCGR magic, section and 4bpp dims as the trainer it covers"
                xxd -l 32 "$van"; xxd -l 32 "$tr_file"
            fi
        else
            bad "the cartridge's own trainer at $tr_lo can be read back"
            tail -2 "$tmp/vantr.log"
        fi
    fi

    if [ "$rom_before" = "$(stamp "$ROM")" ]; then
        ok "the player's image did not move across any of it"
    else
        bad "the player's image did not move across any of it"
    fi
fi

# The porter's answer for a Gen 5 cartridge is a member count, 14285 where
# Gen 4 has 2964, and on its own a count reads like a bound somebody could
# widen.

echo "a Gen 5 cartridge is refused, and the count is the least of it:"

if [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5; set OPENMMO_GEN5_ROM)"
elif porter_missing; then
    :
else
    port5() { python3 "$PORTER" --rom "$SRC5" --out "$tmp/gen5" \
                      "$@"; }

    # It refuses out of its catalog, before reading a byte of the archive.
    if port5 --pokemon chikorita --as piplup > "$tmp/g5.log" 2>&1; then
        bad "the porter refuses --pokemon on a Gen 5 image"
    elif grep -q 'Gen-5 pokegra' "$tmp/g5.log" \
            && [ ! -d "$tmp/gen5/narc" ]; then
        ok "the porter refuses --pokemon on a Gen 5 image, writing nothing"
    else
        bad "the porter refuses --pokemon on a Gen 5 image, writing nothing"
        head -2 "$tmp/g5.log"
    fi

    # And our own driver turns the image away at the door, out of the same
    # catalog line that named the game, rather than starting a recipe and
    # letting the porter answer line by line.
    mkdir -p "$tmp/g5pkg"
    cp "$PKG/mod.toml" "$tmp/g5pkg/mod.toml"
    awk '!/^[[:space:]]*(#|$)/ {print "IRBO", $2, $3, $4; exit}' "$RECIPE" \
        > "$tmp/g5pkg/port.recipe"
    if "$ROOT/tools/modport.sh" "$ENGINE" "$tmp/g5pkg" "$SRC5" "$tmp/g5out" \
            > "$tmp/g5drv.log" 2>&1; then
        bad "the driver refuses a Gen 5 image before it ports a line"
    elif grep -q 'Black (IRBO) does not fill a package' "$tmp/g5drv.log" \
            && grep -q '14285 members' "$tmp/g5drv.log" \
            && [ ! -d "$tmp/g5out" ]; then
        ok "the driver refuses the image itself, naming the game and the shape"
    else
        bad "the driver refuses the image itself, naming the game and the shape"
        tail -2 "$tmp/g5drv.log"
    fi

    # The count itself, derived rather than quoted: ask past any end and the
    # reader answers with how many there are.
    port5 --narc "$GEN5_NARC" --member 99999999 > "$tmp/g5count.log" 2>&1 || true
    n5=$(sed -n "s/.*has \([0-9][0-9]*\) members;.*/\1/p" "$tmp/g5count.log")
    if [ "${n5:-0}" -gt 0 ] && [ $((n5 % 6)) -ne 0 ]; then
        ok "$GEN5_NARC holds $n5 members, which is not a whole number of the"
        ok "  six a species occupies here, species*6+face addresses nothing"
    else
        bad "the Gen 5 archive's count is read, and is not a multiple of six"
        cat "$tmp/g5count.log"
    fi

    # Four members out of the first two blocks. Under this engine's arithmetic
    # member 0 is a character sheet, member 4 a palette, and member 6 the next
    # species' first sheet. None of the three is what is there.
    g5member() { port5 --narc "$GEN5_NARC" --member "$1" > /dev/null 2>&1 \
                 && echo "$tmp/gen5/narc/$GEN5_NARC/$1"; }
    magic() { head -c 4 "$1" | tr -dc 'A-Z'; }
    first() { od -An -N1 -tx1 "$1" | tr -d ' \n'; }

    m0=$(g5member 0); m4=$(g5member 4); m6=$(g5member 6); m20=$(g5member 20)
    if [ -f "${m0:-}" ] && [ -f "${m4:-}" ] && [ -f "${m6:-}" ] \
            && [ -f "${m20:-}" ]; then
        if [ "$(first "$m0")" = 11 ] && [ "$(magic "$m0")" != RGCN ]; then
            ok "member 0 is LZ-compressed, not the bare RGCN the loader reads"
        else
            bad "member 0 is LZ-compressed, not the bare RGCN the loader reads"
            xxd -l 8 "$m0"
        fi

        if [ "$(magic "$m4")" = RECN ] && [ "$(magic "$m6")" = RCMN ]; then
            ok "members 4 and 6 are cell and multi-cell resources, where a Gen 4"
            ok "  block has a palette and the next species' first sheet"
        else
            bad "members 4 and 6 are cell and multi-cell resources"
            xxd -l 8 "$m4"; xxd -l 8 "$m6"
        fi

        # Where the block actually restarts. 20, not 6.
        if [ "$(first "$m20")" = "$(first "$m0")" ] \
                && [ "$(first "$m6")" != "$(first "$m0")" ]; then
            ok "the kind at member 0 comes round again at 20, not at 6"
        else
            bad "the kind at member 0 comes round again at 20, not at 6"
        fi
    else
        bad "the first two blocks of $GEN5_NARC can be read"
    fi
fi

# The plugin composes Black's loop live out of the fill's cell and animation
# banks (mods/openmmo/src/openmmo_blackanim.c); the tool composes the same
# banks to bake the strip.
ANIM_NARC="$PKG/narc/poketool/pokegra/mmo_anim.narc"
if ! command -v python3 >/dev/null 2>&1; then
    echo "  SKIP (no python3 to diff the compositor)"
elif [ ! -f "$SRC5" ]; then
    echo "  SKIP (no Gen 5 cartridge at $SRC5)"
elif [ ! -s "$ANIM_NARC/2340" ]; then
    echo "  SKIP (no cell banks in the fill: python3 mmo/tools/portspecies.py --rom <black>)"
elif ! ${CC:-cc} -O1 -I"$ROOT/mods/openmmo/include" \
        -o "$tmp/blackanim_harness" "$ROOT/tests/blackanim_harness.c" \
        "$ROOT/src/blackcompose.c" -lm > "$tmp/harness.log" 2>&1; then
    bad "tests/blackanim_harness.c builds on the host"
    tail -3 "$tmp/harness.log"
elif python3 "$ROOT/tools/blackanim_diff.py" --rom "$SRC5" --engine "$ENGINE" \
        --harness "$tmp/blackanim_harness" --package "$PKG" \
        390:front 130:front 387:back > "$tmp/animdiff.log" 2>&1; then
    ok "the plugin's live compositor draws what the tool's compose() draws"
else
    bad "the plugin's live compositor draws what the tool's compose() draws"
    tail -4 "$tmp/animdiff.log"
fi

# Which archive a member number belongs to.
OTHERPOKE_SPECIES=201,351,386,412,413,421,422,423,479,487,492,493,494,495
if [ ! -x "$FUSED" ] || [ ! -f "$ROM" ]; then
    echo "  SKIP (no fused build or no ROM to draw pl_otherpoke through)"
elif [ ! -s "$ANIM_NARC/2340" ]; then
    # Without the fill there is nothing to be confused by: the two runs would
    # agree because neither mounted anything, which is a pass that proves
    # nothing. Say so rather than bank it.
    echo "  SKIP (no fill to mount: python3 mmo/tools/portspecies.py --rom <black>)"
else
    for _w in on off; do
        [ "$_w" = on ] && _m="PC_MODS_DIR=$ROOT/mods PC_MODS=imports" || _m=""
        # shellcheck disable=SC2086
        env $_m PC_ROM="$ROM" PC_SAVE="$tmp/other-$_w.sav" PC_FRAMES=4 PC_PACE=0 \
            PC_LAB_SPRITE="$OTHERPOKE_SPECIES" PC_LAB_SPRITE_AT=1 \
            "$FUSED" > "$tmp/other-$_w.log" 2>&1 || true
        grep -E "^pc_lab: sprite species=" "$tmp/other-$_w.log" > "$tmp/other-$_w.txt" || true
    done
    if [ ! -s "$tmp/other-on.txt" ]; then
        bad "the sprite lab drew pl_otherpoke"
        tail -3 "$tmp/other-on.log"
    elif cmp -s "$tmp/other-on.txt" "$tmp/other-off.txt"; then
        ok "every pl_otherpoke page is the cartridge's own, fill mounted or not"
    else
        bad "every pl_otherpoke page is the cartridge's own, fill mounted or not"
        diff "$tmp/other-off.txt" "$tmp/other-on.txt" | head -6
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "import: FAILED"
    exit 1
fi
echo "import: all checks passed"
