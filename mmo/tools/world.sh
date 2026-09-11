#!/usr/bin/env bash
# Check a ported world, or go and stand in it.
#
#   ./mmo/tools/world.sh --check          boot a landmark of every kind,
#                                         headless, and say what each drew
#   ./mmo/tools/world.sh --play           fill what needs filling, start what
#                                         is down, and open the window there
#   ./mmo/tools/world.sh --where          just the list of places and how to
#                                         reach them
#   --rom FILE      the cartridge to fill from (or $OPENMMO_IMPORT_ROM)
#   --user U --pass P   an account to log in as; the server seeds none, so make
#                   one on the website first
#   --mods LIST     override the package list (default hgss)
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/.." && pwd)
ENGINE=${ENGINE_DIR:-$root/../engine/pokeplatinum}
PKG=${OPENMMO_WORLD_PKG:-hgss}
MODS=$PKG
ROM=${OPENMMO_IMPORT_ROM:-}
PC_ROM_PATH=${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}
FUSED=$ROOT/build/fused/pokeplatinum
MAPS=$ROOT/MAPS
mode=
user=
pass=

while [ $# -gt 0 ]; do
    case $1 in
    --check|--play|--where) mode=${1#--}; shift ;;
    --rom)   ROM=$2; shift 2 ;;
    --user)  user=$2; shift 2 ;;
    --pass)  pass=$2; shift 2 ;;
    --mods)  MODS=$2; shift 2 ;;
    -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "world: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$mode" ] || { sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

# The PLACES worth standing in, and why each one is on the list rather than
# whichever came first.
#
#   <map name>  <what it is there to show>
PLACES=$(cat <<'EOF'
goldenrod                a Johto city: buildings, people, its own theme
new_bark                 the town the story starts in
violet                   a city on the far side of the region
route_29                 a road, which should have almost nobody on it
route_32                 a road with trainers on it
goldenrod_pokecenter_1f  a room, and furniture from the other archive
union_cave_1f            a cave, which carries no props at all
pallet                   Kanto, on its own matrix
route_3                  the Kanto road with the most trainers on it
vermilion                the Kanto end of the boat from Olivine
saffron                  the city the Magnet Train arrives in
EOF
)

# A trainer's object event carries no trainer number: Both games put the
# number in the script id, `3000 + trainer - 1` for a single battle and 5000
# for the second half of a double, and run a common script of their own off
# it.
TRAINER_SINGLE=3000
TRAINER_DOUBLE=5000
TRAINER_RANGE=2000

say()  { printf '  %s\n' "$1"; }
head2() { printf '\n%s\n' "$1"; }

# A map's ported header is the rule both halves of this repository compute:
# the base plus the map's own source header. Reading it here rather than
# remembering it is what keeps this script right the day the base moves.
base=$(sed -n 's/^FIRST_FREE_HEADER = \([0-9]*\).*/\1/p' "$ROOT/tools/portmap.py")
header_of() { # NAME
    awk -v n="$1" -v b="$base" '$1 == "hg" && $2 == n { print b + $3; found = 1 }
                                END { exit !found }' "$MAPS"
}

# Where to stand, out of the package rather than out of somebody's memory. A
# hand-typed coordinate is right until a rectangle moves, and then it is a map
# that "draws nothing" for a reason that has nothing to do with the map. This
# asks the matrix the porter actually wrote which cell belongs to the header,
# and stands in the middle of it.
tile_of() { # HEADER -> "x z"
    python3 - "$ROOT/mods/$PKG" "$1" <<'EOF'
import struct, sys
from pathlib import Path
pkg, header = Path(sys.argv[1]), int(sys.argv[2])
rows = {}
for line in (pkg / ".cooked/generated/cooked_maps.txt").read_text().splitlines():
    if line and not line.startswith("#"):
        f = line.split()
        rows[int(f[0])] = f
matrix = int(rows[header][3])
b = (pkg / ".cooked/narc/fielddata/mapmatrix/map_matrix.narc" / str(matrix)).read_bytes()
w, h, has_hdr, has_alt, nlen = b[0], b[1], b[2], b[3], b[4]
p = 5 + nlen
n = w * h
hdrs = list(struct.unpack_from("<%dH" % n, b, p)) if has_hdr else None
if has_hdr:
    p += n * 2
if has_alt:
    p += n
land = list(struct.unpack_from("<%dH" % n, b, p))
# A matrix with a header grid holds a whole region; one without holds this map
# alone, and every filled cell of it is ours.
cell = next(i for i in range(n)
            if land[i] != 0xFFFF and (hdrs is None or hdrs[i] == header))
print("%d %d" % ((cell % w) * 32 + 16, (cell // w) * 32 + 16))
EOF
}

where() {
    head2 "The world is $PKG. Turn it on with --mods $PKG (or \`mods = $PKG\` in launcher.cfg)."
    head2 "Once you are in, /warp takes a NAME, the same name mmo/MAPS uses:"
    printf '%s\n' "$PLACES" | while read -r name what; do
        [ -n "$name" ] || continue
        h=$(header_of "$name" || echo "?")
        printf '  /warp %-24s %-52s (header %s)\n' "$name" "$what" "$h"
    done
    head2 "And /warp takes a fragment, so \`/warp cave\` lists every cave there is."
    head2 "What is worth looking at, in the order it is worth looking:"
    say "1. Does the place look like itself, buildings textured, no white boxes?"
    say "2. Talk to somebody. The line should be their FIRST line: what they say"
    say "   before anything has happened. A line about a badge or an event is a"
    say "   real bug and worth writing down."
    say "3. Walk out of a town onto the road. No load screen, and the banner and"
    say "   the music should change with the map."
    say "4. Go in and out of a door. Then cross between the regions, Indigo"
    say "   Plateau to Route 22, or the boat, or the Magnet Train."
    say "5. Expect silence from some people: 964 of 2,925 keep a script and the"
    say "   rest stand there. Expect no beep before a line, and no cutscene on"
    say "   arrival. Both are deliberate (mmo/MAPFORMATS.md)."
}

fill() {
    if [ -d "$ROOT/mods/$PKG/.cooked" ]; then
        return 0
    fi
    if [ -z "$ROM" ]; then
        echo "world: mods/$PKG has no payload and no cartridge was named." >&2
        echo "       The bytes are the player's, never committed; point --rom" >&2
        echo "       (or OPENMMO_IMPORT_ROM) at a Heart Gold or SoulSilver" >&2
        echo "       image and this will fill it." >&2
        exit 1
    fi
    echo "world: filling mods/$PKG from $ROM (a few minutes, once)"
    "$ROOT/tools/modport.sh" "$ENGINE" "$ROOT/mods/$PKG" "$ROM"
}

check() {
    [ -x "$FUSED" ] || { echo "world: no fused build; run make -C mmo fused" >&2; exit 1; }
    [ -f "$PC_ROM_PATH" ] || { echo "world: no ROM at $PC_ROM_PATH (PC_ROM)" >&2; exit 1; }
    fill
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    settle=$ENGINE/pc/replays/lab-settle.txt
    printf '\n%-24s %6s %7s %7s %8s %8s  %s\n' \
        "map" "header" "tiles" "people" "trainers" "asserts" "verdict"
    bad=0
    printf '%s\n' "$PLACES" | while read -r name what; do
        [ -n "$name" ] || continue
        h=$(header_of "$name") || { printf '%-24s  no row in mmo/MAPS\n' "$name"; continue; }
        at=$(tile_of "$h") || { printf '%-24s  no cell of its own in the package\n' "$name"; continue; }
        printf 'name SHORT\nmap %s %s 1\n' "$h" "$at" > "$tmp/lab"
        env OPENMMO_ASSERT=warn OPENMMO_HUD=0 \
            PC_ROM="$PC_ROM_PATH" PC_SAVE="$tmp/save" PC_PACE=0 \
            PC_MODS_DIR="$ROOT/mods" PC_MODS="$MODS" \
            PC_FRAMES=2500 PC_LAB="$tmp/lab" PC_LAB_AT=1800 \
            PC_INPUT="$settle" PC_LAB_MAPSCAN="$tmp/scan" \
            "$FUSED" > "$tmp/log" 2>&1 || true
        got=$(sed -n 's/^map \([0-9]*\)$/\1/p' "$tmp/scan" 2>/dev/null | head -1)
        # `grep -c` prints its zero and exits non-zero, so a `|| echo 0`
        # here would print the count twice and every test below would be on a
        # two-line string. `|| :` keeps the count grep already printed.
        tiles=$(grep -c '^tile' "$tmp/scan" 2>/dev/null || :)
        people=$(grep -c '^object' "$tmp/scan" 2>/dev/null || :)
        trainers=$(awk -v a="$TRAINER_SINGLE" -v b="$TRAINER_DOUBLE" \
                       -v r="$TRAINER_RANGE" \
                       '$1 == "object" && $5 + 0 >= a && $5 + 0 < b + r { n++ }
                        END { print n + 0 }' "$tmp/scan" 2>/dev/null || :)
        asserts=$(grep -c 'assertion failed' "$tmp/log" 2>/dev/null || :)
        # A room is one 32x32 cell and the lab's own scanner reads past its
        # edge, which a vanilla Pokemon Center does identically (2403 of them).
        # So the assertion count is only a verdict on a map big enough to hold
        # the scan window; on a small one it says nothing and is not read.
        verdict=ok
        [ "${got:-none}" = "$h" ] || verdict="landed on ${got:-nothing}, not $h"
        [ "${tiles:-0}" -gt 0 ] || verdict="no terrain under the player"
        case $what in
        *trainers*) [ "${trainers:-0}" -gt 0 ] || \
            verdict="nobody on it is a trainer" ;;
        esac
        printf '%-24s %6s %7s %7s %8s %8s  %s\n' \
            "$name" "${got:-?}" "$tiles" "$people" "$trainers" "$asserts" \
            "$verdict"
        [ "$verdict" = ok ] || echo x >> "$tmp/bad"
    done
    if [ -s "$tmp/bad" ]; then
        echo; echo "world: $(wc -l < "$tmp/bad") place(s) did not answer"; exit 1
    fi
    echo
    echo "world: every place answered. What a machine can check, it has;"
    echo "       the rest is --play."
}

play() {
    fill
    [ -x "$FUSED" ] || make -C "$ROOT" fused
    if ! ss -ltn 2>/dev/null | grep -q ':2106 '; then
        echo "world: no login server on 2106, starting the servers"
        "$REPO/start-server.sh"
    fi
    where
    echo
    set -- --online --world --mods "$MODS"
    [ -n "$user" ] && set -- "$@" --user "$user"
    [ -n "$pass" ] && set -- "$@" --pass "$pass"
    exec "$ROOT/play.sh" "$@"
}

case $mode in
where) where ;;
check) check ;;
play)  play ;;
esac
