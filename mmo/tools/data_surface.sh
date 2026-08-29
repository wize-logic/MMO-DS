#!/bin/sh
# The guard on the two checkouts of one decomp.
#
#   data_surface.sh [--engine DIR] [--decomp DIR] [--repo DIR]
#                   [--sources FILE] [--gradle FILE] --check
#          and the declared generator set is the one gradle registers; exit 1
#          otherwise, naming the files that differ. SKIPs (exit 0) when either
#          checkout is absent.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "$root/.." && pwd)

engine=${ENGINE:-$root/../engine/pokeplatinum}
decomp=
sources=
gradle=
mode=

while [ $# -gt 0 ]; do
    case $1 in
    --engine) engine=$2; shift 2 ;;
    --decomp) decomp=$2; shift 2 ;;
    --repo) repo=$2; shift 2 ;;
    --root) root=$2; shift 2 ;;
    --sources) sources=$2; shift 2 ;;
    --gradle) gradle=$2; shift 2 ;;
    --check) mode=check; shift ;;
    *) echo "data_surface: unknown argument $1" >&2; exit 2 ;;
    esac
done

[ -n "$decomp" ] || decomp=$("$root/tools/decomp_dir.sh" pokeplatinum 2>/dev/null || true)
[ -n "$decomp" ] || decomp=$repo/engine/pokeplatinum
[ -n "$sources" ] || sources=$root/DATA_SOURCES
[ -n "$gradle" ] || gradle=$repo/codegen/build.gradle.kts

if [ "$mode" != check ]; then
    echo "data_surface: --check is required" >&2
    exit 2
fi

if [ ! -f "$sources" ]; then
    echo "datacheck: FAIL, no tree list at $sources"
    exit 1
fi

# Both checkouts have to be here for the comparison to mean anything. Absent is a
# SKIP and not a pass-by-default: this is the only check that sees both at once.
if [ ! -d "$engine/res" ]; then
    echo "datacheck: SKIP (no engine checkout at $engine)"
    exit 0
fi
if [ ! -d "$decomp/res" ]; then
    echo "datacheck: SKIP (no tree for codegen at $decomp)"
    exit 0
fi

# One tree can serve both halves, and since the DS decomps moved to maintained
# checkouts outside the repo it usually does: $ENGINE_DIR is the pokeplatinum
# checkout codegen reads.
same=0
if [ "$(CDPATH= cd -- "$engine" && pwd -P)" = "$(CDPATH= cd -- "$decomp" && pwd -P)" ]; then
    same=1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

rows() { grep -vE '^[[:space:]]*(#|$)|^readers:' "$sources"; }

# One tree, listed as every file under it with its hash, sorted by path so the
# two listings line up and `diff` names exactly what moved.
listing() {
    (cd "$1" && find "$2" -type f -print0 2>/dev/null | LC_ALL=C sort -z |
        xargs -0 -r sha256sum) 2>/dev/null || true
}

bad=0
n=0
moved=0

while read -r tree readers; do
    [ -n "$tree" ] || continue
    n=$((n + 1))
    if [ "$same" = 1 ]; then
        [ -d "$engine/$tree" ] || {
            bad=1
            echo "  $tree MISSING from $engine. Read by: ${readers:-nothing}."
        }
        continue
    fi
    if [ ! -d "$engine/$tree" ] || [ ! -d "$decomp/$tree" ]; then
        bad=1
        printf ' %-12s MISSING, ' "$tree"
        [ -d "$engine/$tree" ] || printf 'not in the engine checkout. '
        [ -d "$decomp/$tree" ] || printf 'not in the tree codegen reads. '
        echo "Read by: ${readers:-nothing}."
        continue
    fi
    listing "$engine" "$tree" >"$tmp/e"
    listing "$decomp" "$tree" >"$tmp/d"
    if cmp -s "$tmp/e" "$tmp/d"; then
        continue
    fi
    moved=$((moved + 1))
    bad=1
    # Only the added/removed lines, and only the path half of each: a hunk
    # header taken for a filename would read as a file nobody can go and look at.
    files=$(diff "$tmp/e" "$tmp/d" |
        sed -n 's|^[<>] [0-9a-f]\{64\}  ||p' | sort -u)
    count=$(echo "$files" | grep -c . || true)
    echo " $tree DIFFERS between the two checkouts, $count file(s), read by: ${readers:-nothing}"
    echo "$files" | head -12 | sed 's/^/        /'
    [ "$count" -gt 12 ] && echo "        ... and $((count - 12)) more"
done <<EOF
$(rows)
EOF

# The generators registered against the DS decomp, read out of gradle rather than
# remembered here. A block that mentions ndsDataDir reads it directly;
# mapRegionSources is the map generator, which takes the DS decomp as one of its
# regions. Either way it is a reader and its trees must be declared.
derive_readers() {
    [ -f "$gradle" ] || return 0
    awk '
      /^[[:space:]]*register\("/ {
        name = $0; sub(/^[^"]*"/, "", name); sub(/".*/, "", name)
        depth = 0; body = ""
      }
      name != "" {
        body = body $0
        depth += gsub(/\{/, "{"); depth -= gsub(/\}/, "}")
        if (depth <= 0) {
          if (body ~ /ndsDataDir|mapRegionSources/) print name
          name = ""
        }
      }
    ' "$gradle" | sort -u
}

declared=$(sed -n 's|^readers:[[:space:]]*||p' "$sources" | head -1 | tr ' ' '\n' | grep . | sort -u || true)
actual=$(derive_readers)

if [ -f "$gradle" ] && [ "$declared" != "$actual" ]; then
    bad=1
    echo "  the generators reading the DS decomp are not the ones $sources declares:"
    echo "$declared" >"$tmp/declared"
    echo "$actual" >"$tmp/actual"
    comm -13 "$tmp/declared" "$tmp/actual" | sed 's/^/        now reads it and is undeclared: /'
    comm -23 "$tmp/declared" "$tmp/actual" | sed 's/^/        declared and no longer reads it: /'
    echo "        Add the trees it reads above, or drop it, then re-check."
fi

# Every declared reader owns at least one tree. Adding a name to `readers:` and
# forgetting its tree would otherwise pass both checks above and guard nothing.
for r in $declared; do
    if ! rows | awk -v r="$r" '{ for (i = 2; i <= NF; i++) if ($i == r) found = 1 } END { exit !found }'; then
        bad=1
        echo "  $r is declared a reader of the DS decomp but no tree lists it"
    fi
done

if [ "$bad" -eq 0 ]; then
    # Context only, and only when both are git: how far apart the pins are, now
    # that the answer that matters is already known.
    span=
    dpin=$(git -C "$decomp" rev-parse HEAD 2>/dev/null || true)
    if [ -n "$dpin" ] && git -C "$engine" cat-file -e "$dpin" 2>/dev/null; then
        c=$(git -C "$engine" rev-list --count "$dpin"..HEAD 2>/dev/null || true)
        [ -n "$c" ] && span=", engine $c commit(s) ahead of the submodule and none of it here"
    fi
    if [ "$same" = 1 ]; then
        echo "datacheck: ok ($n data trees, one checkout ($engine) serving both" \
             "halves, nothing to drift)"
        exit 0
    fi
    echo "datacheck: ok ($n data trees identical in both checkouts$span)"
    exit 0
fi

echo "datacheck: $moved of $n trees the client and the server both read have"
echo "  diverged between the two checkouts. The client would render one world"
echo "  while the server computed another, and every check on either side would"
echo " still pass, each round-trips its own copy. Bring the two to the same"
echo "  upstream content before trusting either. mmo/DATA_SOURCES says who reads"
echo "  what and why the two pins cannot be one."
exit 1
