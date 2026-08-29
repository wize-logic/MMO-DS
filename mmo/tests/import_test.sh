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

print("%d members, %d gfx rows, %d carried, sequences %s and %s"
      % (len(members), len(gfx), len(carried), seq[0][0], seq[1][0]))
PY
    then
        ok "every planted member is the cartridge's own bytes ($(cat "$tmp/folback.log"))"
    else
        bad "every planted member is the cartridge's own bytes"
        tail -2 "$tmp/folback.log" | sed 's/^/       /'
    fi

    # And the whole thing loads. 568 members and 1,132 cooked rows is a bigger
    # package than anything else in this repo hands the port.
    if [ ! -x "$FUSED" ] || [ ! -f "$ROM" ] \
            || [ ! -f "$ENGINE/pc/replays/lab-settle.txt" ]; then
        echo "  SKIP (no fused build, Platinum image or settle replay to load it with)"
    else
        printf 'name SHORT\n' > "$tmp/fol.lab"
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
        elif grep -q '568 members' "$tmp/folboot.log" \
                && grep -q 'cooked billboard sequence 64' "$tmp/folboot.log" \
                && grep -q 'cooked billboard sequence 65' "$tmp/folboot.log"
        then
            ok "the fused build loads all 568 members and both sequences"
        else
            bad "the fused build loads all 568 members and both sequences"
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
        for code in CPUE ADAE IRBO; do
            slot=$("$CLIENT" cartridges "$code" 2>/dev/null | head -1 | cut -d' ' -f2)
            grep -q "$slot" "$ROOT/CARTRIDGES" || agree=0
        done
        if [ "$agree" = 1 ]; then
            ok "the client and the registry name the same slot for a code"
        else
            bad "the client and the registry name the same slot for a code"
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
DIA=${OPENMMO_DIAMOND_ROM:-$REPO/roms/pokediamond.nds}
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
                ok " and a different sheet size, $(wc -c < "$van" | tr -d ' ') bytes covered by $(wc -c < "$tr_file" | tr -d ' ')"
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
        ok " six a species occupies here, species*6+face addresses nothing"
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

if [ "$fail" -ne 0 ]; then
    echo "import: FAILED"
    exit 1
fi
echo "import: all checks passed"
