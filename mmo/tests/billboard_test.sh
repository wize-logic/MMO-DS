#!/bin/sh
# A cooked overworld body that is not shaped like a
# person, measured by the rasterizer rather than by the log.
#
#   1. the fixture is honest before anything is booted: the two members it
#      plants really are a 32x32 and a 64x64 follower, read out of the player's
#      own cartridge through the engine port's NARC reader.
#   2. three boots survive, no peer, a peer wearing the small row, a peer
#      wearing the large one, because a cooked row that crashes the field is
#      the failure this is most likely to cause.
#   3. the facings are built from the numbers the cook wrote, not from a
#      constant: 4 facings of 24 frames is what member 294's own step list says,
#      and the mod says so back.
#   4. The measurement. The quad the small row drew is 32 wide; the quad the
#      large row drew is 64; and the second is exactly twice the first. Under
#      the youngster clone both would be 32, which is what makes this the check
#      that would have failed before the patch and passes after it.
#   5. a two-column row still draws. `mods/bodies` is committed with the old
#      `276 470` shape and nothing about it changed, so the compatibility claim
#      is tested against the actual package rather than asserted.
#   6. the new refusal is reached on purpose: a row naming a model with no
#      sequence is a half-written renderer row, and the port stops rather than
#      drawing half of one.
set -eu

ROOT=${1:?usage: billboard_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: billboard_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: billboard_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
SRC4="${OPENMMO_GEN4_ROM:-$(CDPATH= cd -- "$ROOT/.." && pwd)/roms/pokesoulsilver.nds}"

# Where a cooked row lands. The gfx ids are the first two free OBJ_EVENT_GFX
# (mods/bodies uses the first for its own person, and this runs instead of it,
# not beside it); the members are the first two free mmodel appends, and the
# sequence is the third. Appends must be contiguous from the ROM's own count,
# which is what makes these three consecutive rather than arbitrary.
GFX_SMALL=276
GFX_LARGE=277
MEM_SMALL=470
MEM_LARGE=471
MEM_SEQ=472
# Past the engine's own frame sequences, which stop at 22.
SEQ_ID=64

if [ ! -x "$FUSED" ]; then
    echo "billboard: SKIP (no fused build: run \`make -C mmo fused\`)"
    exit 0
fi
if [ ! -f "$ROM" ] || [ ! -f "$SETTLE" ]; then
    echo "billboard: SKIP (no ROM or input script under $ENGINE)"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "billboard: SKIP (no python3 to cut the fixture with)"
    exit 0
fi
if [ ! -f "$SRC4" ]; then
    echo "billboard: SKIP (no Gen 4 cartridge at $SRC4; set OPENMMO_GEN4_ROM)"
    exit 0
fi

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

printf 'name SHORT\n' > "$tmp/min.lab"

echo "a cooked body that is not shaped like a person:"

# --- 1. the fixture, cut from the player's own image ------------------------
if ! python3 - "$ROOT" "$SRC4" "$tmp/mods/followtest" \
        "$MEM_SMALL" "$MEM_LARGE" "$MEM_SEQ" > "$tmp/fixture.log" 2>&1 <<'PY'
import sys
from pathlib import Path

root, rom_path, out, m_small, m_large, m_seq = sys.argv[1:7]
root = Path(root)
sys.path.insert(0, str(root / "tools"))
import nsbtx                                                # noqa: E402
import importlib.util                                       # noqa: E402

spec = importlib.util.spec_from_file_location(
    "gf", root / "tools" / "gen_followers.py")
gf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gf)

hg = gf.decomp_dir("pokeheartgold")
if hg is None or not hg.is_dir():
    raise SystemExit("no heartgold checkout to read the follower table from")

sys.path.insert(0, str(gf._engine_pc()))
from modport import NitroRom                                # noqa: E402

rom = NitroRom(Path(rom_path))
mm = rom.narc_members(gf.SRC_MMODEL)

rows, expansion, base, lo, hi, spare = gf.build(hg)
by_name = {r[5]: r for r in rows}

def member_and_size(species_name):
    row = by_name[species_name]
    member = row[4]
    size = gf._shape(nsbtx, mm[member])
    if isinstance(size, str):
        raise SystemExit("%s member %d is not follower art: %s"
                         % (species_name, member, size))
    return member, size

small, small_px = member_and_size("PIKACHU")
large, large_px = member_and_size("STEELIX")
if small_px != 32 or large_px != 64:
    raise SystemExit("expected a 32 and a 64: got %d and %d"
                     % (small_px, large_px))

seq, why = gf._walk_sequence(mm)
if seq is None:
    raise SystemExit(why)

pkg = Path(out)
narc = pkg / ".cooked/narc/data/mmodel/mmodel.narc"
narc.mkdir(parents=True, exist_ok=True)
(pkg / ".cooked/generated").mkdir(parents=True, exist_ok=True)
(narc / m_small).write_bytes(mm[small])
(narc / m_large).write_bytes(mm[large])
(narc / m_seq).write_bytes(mm[seq])
(pkg / ".cooked/digest").write_text("v1 %016x\n" % 0xCBF29CE484222325)
(pkg / "mod.toml").write_text(
    'id = "followtest"\nname = "Follower door fixture"\nversion = "1.0.0"\n')
print("pikachu member %d (%dpx), steelix member %d (%dpx), sequence member %d"
      % (small, small_px, large, large_px, seq))
PY
then
    bad "the fixture is cut from the cartridge"
    tail -2 "$tmp/fixture.log"
    exit 1
fi
ok "the fixture is a real 32x32 and a real 64x64 follower ($(cat "$tmp/fixture.log"))"

gen="$tmp/mods/followtest/.cooked/generated"
printf '%s %s 0 %s\n%s %s 5 %s\n' \
    "$GFX_SMALL" "$MEM_SMALL" "$SEQ_ID" \
    "$GFX_LARGE" "$MEM_LARGE" "$SEQ_ID" > "$gen/billboard_gfx.txt"
# 4 facings of 24 frames is member 294's own step list: 16 steps six frames
# apart over a 96-frame timeline, in four groups of [a,b,a,b].
printf '%s %s 4 24\n' "$SEQ_ID" "$MEM_SEQ" > "$gen/billboard_seq.txt"

# --- 2. three boots ---------------------------------------------------------
boot() {
    _which=$1
    shift
    env OPENMMO_ASSERT=warn OPENMMO_HUD=0 OPENMMO_LABELS=0 "$@" \
        PC_ROM="$ROM" PC_SAVE="$tmp/$_which.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=1800 PC_PACE=0 PC_INPUT="$SETTLE" \
        PC_DUMP_POLYS=1799-1799 PC_DUMP_POLYS_FILE="$tmp/$_which.polys" \
        "$FUSED" > "$tmp/$_which.log" 2>&1
}

MODS_ENV="PC_MODS_DIR=$tmp/mods PC_MODS=followtest"
for which in alone small large; do
    case $which in
    alone) extra="" ;;
    small) extra="OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_GFX=$GFX_SMALL" ;;
    large) extra="OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_GFX=$GFX_LARGE" ;;
    esac
    rc=0
    # shellcheck disable=SC2086
    boot "$which" $MODS_ENV $extra || rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "the field survives a peer wearing the cooked row ($which, exit $rc)"
        tail -3 "$tmp/$which.log"
        exit 1
    fi
done
ok "the field loads with a cooked follower row on it, three ways"

# --- 3. the facings are the cook's numbers ----------------------------------
if grep -q "cooked billboard sequence $SEQ_ID -> 4 facing(s) of 24 frames" \
        "$tmp/small.log"; then
    ok "the facing list is built from the sequence the cook measured"
else
    bad "the facing list is built from the sequence the cook measured"
    grep -h 'cooked billboard' "$tmp/small.log" | head -2 | sed 's/^/       /'
fi

# --- 4. the measurement -----------------------------------------------------
#
# The largest axis-aligned rectangle in a peer's polygon list that is not in the
# empty map's, exactly as label_test.sh finds an avatar's quad.
quad() {
    python3 - "$tmp/alone.polys" "$1" <<'PY'
import re, sys

def rects(path):
    out = []
    for line in open(path):
        if not line.startswith('   '):
            continue
        vs = [t for t in line.split() if re.fullmatch(r'-?\d+,-?\d+', t)]
        if len(vs) != 4:
            continue
        pts = [tuple(map(int, v.split(','))) for v in vs]
        xs = sorted(set(p[0] for p in pts))
        ys = sorted(set(p[1] for p in pts))
        if len(xs) == 2 and len(ys) == 2:
            out.append((xs[0], ys[0], xs[1], ys[1]))
    return out

base = set(rects(sys.argv[1]))
new = [r for r in rects(sys.argv[2]) if r not in base]
if not new:
    print("0")
else:
    r = max(new, key=lambda q: (q[2] - q[0]) * (q[3] - q[1]))
    print(r[2] - r[0])
PY
}

w_small=$(quad "$tmp/small.polys")
w_large=$(quad "$tmp/large.polys")

if [ "$w_small" = "32" ]; then
    ok "the 32x32 row drew a 32-wide quad"
else
    bad "the 32x32 row drew a 32-wide quad (got $w_small)"
fi
if [ "$w_large" = "64" ]; then
    ok "the 64x64 row drew a 64-wide quad, which the youngster clone cannot"
else
    bad "the 64x64 row drew a 64-wide quad (got $w_large)"
fi
if [ "$w_small" != "0" ] && [ "$w_large" = "$((w_small * 2))" ]; then
    ok "and the large one is exactly twice the small one"
else
    bad "and the large one is exactly twice the small one ($w_small vs $w_large)"
fi

# --- 5. a two-column row is untouched ---------------------------------------
#
# mods/bodies is committed with the old shape and this run changes nothing about
# it, so the compatibility claim is measured against the package rather than
# asserted about it.
if [ ! -f "$ROOT/mods/bodies/.cooked/generated/billboard_gfx.txt" ]; then
    echo "  SKIP (no mods/bodies to check the old row shape against)"
elif awk 'NF && $1 !~ /^#/ { if (NF != 2) bad = 1 } END { exit bad }' \
        "$ROOT/mods/bodies/.cooked/generated/billboard_gfx.txt"; then
    rc=0
    boot person PC_MODS_DIR="$ROOT/mods" PC_MODS=bodies \
        OPENMMO_FAKE_ENTITY=1 OPENMMO_FAKE_GFX=276 || rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "a two-column row still draws (exit $rc)"
        tail -3 "$tmp/person.log"
    elif [ "$(quad "$tmp/person.polys")" = "32" ]; then
        ok "a two-column row still draws as the youngster it always did"
    else
        bad "a two-column row still draws as the youngster it always did (got $(quad "$tmp/person.polys"))"
    fi
else
    bad "mods/bodies no longer carries a two-column row to check against"
fi

# --- 6. half a renderer row is refused --------------------------------------
printf '%s %s 0\n' "$GFX_SMALL" "$MEM_SMALL" > "$gen/billboard_gfx.txt"
rc=0
boot halfrow $MODS_ENV || rc=$?
if [ "$rc" -ne 0 ] && grep -q 'names a model and no sequence' "$tmp/halfrow.log"
then
    ok "a row naming a model with no sequence is refused by name"
else
    bad "a row naming a model with no sequence is refused by name (exit $rc)"
    tail -2 "$tmp/halfrow.log" | sed 's/^/       /'
fi

if [ "$fail" -eq 0 ]; then
    echo "billboard: all checks passed"
else
    echo "billboard: FAILED"
fi
exit "$fail"
