#!/bin/sh
# The folder a player drops cartridges into, read.
#
#   scan_cartridges.sh [folder] [--quiet]
#   * A compressed archive is not opened. The official client bundles libarchive so a player
#     can drop a .zip in, and pays for it with a dependency and a decompression
#     bomb. A sentence telling somebody to unzip their file costs nothing and
#     is refused by name here, which is the whole feature at none of the price.
#   * A cartridge is read where it lies. The official client copies on Android and
#     references on desktop; this is desktop. Copying a 128MB image the player
#     already owns into a second place makes two things to keep and two things
#     to delete, and the porter never writes to the image, import_test.sh
#     checks that the player's file does not move across a whole fill.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "$root/.." && pwd)

folder=${1:-$repo/roms}
case "${1:-}" in --quiet) folder=$repo/roms ;; esac
quiet=0
for a in "$@"; do [ "$a" = --quiet ] && quiet=1; done

ENGINE=${ENGINE_DIR:-$root/../engine/pokeplatinum}
PORTER=$ENGINE/pc/modport.py
PYTHON=${PYTHON:-python3}
REGISTRY=${OPENMMO_CARTRIDGES:-$root/CARTRIDGES}
MANIFEST=$folder/cartridges.found

if [ ! -d "$folder" ]; then
    echo "scan: no folder at $folder, make it and put a cartridge in it" >&2
    exit 1
fi
if [ ! -f "$PORTER" ]; then
    echo "scan: no porter at $PORTER (set ENGINE_DIR)" >&2
    exit 1
fi
if [ ! -f "$REGISTRY" ]; then
    echo "scan: no cartridge registry at $REGISTRY" >&2
    exit 1
fi

# code slot name status kinds for one code, or nothing. Same rows the driver
# dispatches on; matching the whole code first and the game's three after, so
# an unread build is still attributed to its slot.
cart_row() {
    awk -v want="$1" '
        /^[[:space:]]*(#|$)/ { next }
        $1 == "slot" { line = $0
                       sub(/^slot[[:space:]]+[^ \t]+[[:space:]]+"/, "", line)
                       sub(/"[[:space:]]*$/, "", line)
                       name[$2] = line; next }
        $1 == "lang" { next }
        {
            if ($1 == want)      { exact = $1 "|" $2 "|" $3 "|" $4 }
            else if (substr($1, 1, 3) == substr(want, 1, 3) && slot == "")
                                 { slot = $1 "|" $2 "|" $3 "|" $4 }
        }
        END {
            row = (exact != "") ? exact : slot
            if (row == "") exit 1
            split(row, f, "|")
            print f[1] "|" f[2] "|" name[f[2]] "|" f[3] "|" f[4]
        }
    ' "$REGISTRY"
}

tmp=$(mktemp) || exit 1
trap 'rm -f "$tmp"' EXIT INT HUP TERM

found=0
skipped=0
for path in "$folder"/*; do
    [ -f "$path" ] || continue
    name=$(basename "$path")
    case "$name" in
    cartridges.found|.gitignore) continue ;;
    *.zip|*.7z|*.rar|*.tar|*.gz|*.xz|*.bz2)
        # Refused by name rather than unopened: a player who dropped an archive
        # in has done something reasonable and needs one sentence back.
        printf 'archive %s, unzip it first; this client does not open archives\n' \
            "$name" >> "$tmp"
        skipped=$((skipped + 1))
        continue ;;
    esac

    if ! ident=$("$PYTHON" "$PORTER" --rom "$path" --identify 2>&1); then
        # The porter's own refusal, first line, so the record says why this
        # file was not a cartridge rather than leaving it out.
        why=$(printf '%s' "$ident" | head -1 | sed 's/^port: //')
        printf 'not-a-cartridge %s, %s\n' "$name" "$why" >> "$tmp"
        skipped=$((skipped + 1))
        continue
    fi

    code=${ident#port: }
    code=${code%% *}
    row=$(cart_row "$code") || row=
    if [ -z "$row" ]; then
        printf 'unknown %s %s, not a cartridge this client knows\n' \
            "$code" "$name" >> "$tmp"
        skipped=$((skipped + 1))
        continue
    fi
    row_code=$(echo "$row" | cut -d'|' -f1)
    slot_name=$(echo "$row" | cut -d'|' -f3)
    status=$(echo "$row" | cut -d'|' -f4)
    kinds=$(echo "$row" | cut -d'|' -f5)
    [ "$row_code" = "$code" ] || status=unread
    printf '%s %s %s "%s" %s\n' "$status" "$code" "$name" "$slot_name" "$kinds" \
        >> "$tmp"
    found=$((found + 1))
done

{
    echo "# cartridges.found, written by mmo/tools/scan_cartridges.sh."
    echo "# Every file in this folder and what it turned out to be. Images are"
    echo "# read where they lie; nothing here is a copy, and nothing was opened"
    echo "# that was not handed to the porter."
    echo "#"
    echo "# <status> <code> <file> \"<cartridge>\" <kinds it serves>"
    echo "# read = may fill a package. Everything else says why not."
    [ -s "$tmp" ] && sort "$tmp"
} > "$MANIFEST"

if [ "$quiet" = 0 ]; then
    sed -n '/^[^#]/p' "$MANIFEST" | sed 's/^/  /'
    echo "scan: $found cartridge(s), $skipped other file(s) in $folder"
    echo "scan: written to $MANIFEST"
fi
