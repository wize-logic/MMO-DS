#!/bin/sh
# The second half of the engine contract.
#
#   * a file we patch is hashed as what our patch is handed, after
#     strip_asm.py and after the engine's own pc/patches diff
#     (engine_pipeline.sh). That is the surface our hunks' context lives on, and
#     it folds in changes to those two steps as well.
#   * everything else, headers our mod sources include, the stub list, the
#     engine Makefile whose MODS/EXTRA_OBJS hooks the fused build rides on, is
#     hashed as its bytes.
#   engine_surface.sh [--engine DIR] [--root DIR] [--manifest FILE] --check
#   engine_surface.sh [...] --update
#          should; exit 1 otherwise, naming each one. SKIPs (exit 0) with no
#          engine checkout.
#          patched file or newly included engine header that is missing. Run it
#          only as part of a pin bump, the manifest's whole value is that it
#          records a state something verified.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/engine_pipeline.sh"

engine=${ENGINE:-$root/../engine/pokeplatinum}
manifest=$root/ENGINE_SURFACE
mode=

while [ $# -gt 0 ]; do
    case $1 in
    --engine) engine=$2; shift 2 ;;
    --root) root=$2; shift 2 ;;
    --manifest) manifest=$2; shift 2 ;;
    --check) mode=check; shift ;;
    --update) mode=update; shift ;;
    *) echo "engine_surface: unknown argument $1" >&2; exit 2 ;;
    esac
done

if [ -z "$mode" ]; then
    echo "engine_surface: one of --check or --update is required" >&2
    exit 2
fi

if [ ! -d "$engine/include" ]; then
    echo "pincheck: SKIP (no engine checkout at $engine)"
    exit 0
fi

patches=$root/mods/openmmo/patches
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# The pin, parsed the same way the Makefile parses it, so the two never disagree
# about which commit is claimed.
pin=$(grep -vE '^[[:space:]]*(#|$)' "$root/ENGINE_COMMIT" 2>/dev/null | head -1 | tr -d '[:space:]' || true)

# Hash one engine path as the fused build consumes it. Prints the hash, or
# "GONE" if the engine no longer has the file, or "BASE-GONE" if the engine's
# own diff for it stopped applying.
surface_hash() {
    _sh_file=$1
    if [ ! -f "$engine/$_sh_file" ]; then
        echo GONE
        return
    fi
    if [ ! -f "$patches/$_sh_file.patch" ]; then
        sha256sum <"$engine/$_sh_file" | cut -d' ' -f1
        return
    fi
    rm -rf "$tmp/w"
    mkdir -p "$tmp/w/$(dirname "$_sh_file")"
    cp "$engine/$_sh_file" "$tmp/w/$_sh_file"
    if ! engine_pipeline_prepare "$engine" "$_sh_file" "$tmp/w"; then
        echo BASE-GONE
        return
    fi
    sha256sum <"$tmp/w/$_sh_file" | cut -d' ' -f1
}

# What the manifest must cover, derived rather than remembered.
#
#   * every file we patch, a patch nobody hashes is the case this exists for
#   * every engine header a mod source includes and that resolves under
#     $ENGINE/include. An include that resolves nowhere is reported, not
#     ignored: generated/movement_actions.h is real and is built into
#     build/pc/geninclude, so no committed path can stand for it.
#   * three build-pipeline inputs that are not source at all: pc/Makefile, whose
#     MODS_DIR / MODS / EXTRA_OBJS hooks are the whole mechanism the fused build
#     rides on; pc/stubs.list, where a symbol appearing or vanishing decides
#     whether a dropped object still links; and tools/armrec/strip_asm.py, the
#     first step every patch is cut through.
#   * the runtime content door: pc_modfs.c / pc_modfs.h, the NARC hook, the
#     MapHeader_Lookup overlay, and the extra-prop load. The fused play path
#     forwards PC_MODS / PC_MODS_DIR into these; a pin that does not include
#     them is a pin that cannot serve a package.
required_paths() {
    echo "pc/Makefile"
    echo "pc/stubs.list"
    echo "tools/armrec/strip_asm.py"
    echo "pc/src/pc_modfs.c"
    echo "pc/include/pc_modfs.h"
    echo "src/narc.c"
    echo "src/map_header.c"
    echo "src/overlay005/area_data.c"
    echo "src/overlay005/map_prop_material_shape.c"
    find "$patches" -name '*.patch' | sed "s|^$patches/||; s|\.patch$||"
    for src in "$root"/mods/openmmo/src/*.c; do
        [ -f "$src" ] || continue
        sed -n 's|^#include "\([^"]*\)".*|\1|p' "$src"
    done | sort -u | while read -r inc; do
        case $inc in
        ../*) continue ;;
        esac
        if [ -f "$engine/include/$inc" ]; then echo "include/$inc"; fi
    done
    true
}

unresolved_includes() {
    for src in "$root"/mods/openmmo/src/*.c; do
        [ -f "$src" ] || continue
        sed -n 's|^#include "\([^"]*\)".*|\1|p' "$src"
    done | sort -u | while read -r inc; do
        case $inc in
        ../*) continue ;;
        esac
        if [ -f "$engine/include/$inc" ]; then continue; fi
        # The pc/ layer's own headers live beside the host code, not in include/.
        if [ -n "$(find "$engine/pc" -name "$(basename "$inc")" -print -quit 2>/dev/null)" ]; then
            continue
        fi
        echo "$inc"
    done
    true
}

manifest_paths() {
    [ -f "$manifest" ] || return 0
    grep -vE '^[[:space:]]*(#|$)' "$manifest" | awk '{ print $2 }'
}

if [ "$mode" = update ]; then
    { manifest_paths; required_paths; } | sort -u >"$tmp/paths"
    {
        echo "# Engine paths this client depends on by content, and the hash each had"
        echo "# when mmo/ENGINE_COMMIT was last verified. Regenerated only by a pin"
        echo "# bump: mmo/tools/engine_surface.sh --update, checked by \`make -C mmo"
        echo "# pincheck\`. The policy this file serves, when the pin moves at all"
        echo "# and what a bump has to prove, is mmo/mods/openmmo/README.md."
        echo "#"
        echo "# A path we patch is hashed as what the compile hands our patch (after"
        echo "# strip_asm.py and the engine's own pc/patches diff); everything else is"
        echo "# hashed as its bytes. Do not edit by hand: a hash nobody computed is"
        echo "# worse than no hash, because it reads as verified."
        echo "#"
        echo "# pin: $pin"
        echo ""
        while read -r p; do
            [ -n "$p" ] || continue
            h=$(surface_hash "$p")
            # Never record a non-hash. "GONE" written into the manifest would go
            # on matching a file that is still missing, for ever, and read as a
            # verified line every time.
            case $h in
            GONE | BASE-GONE)
                echo "engine_surface: refusing to record $p, $h" >&2
                echo "REFUSED"
                continue
                ;;
            esac
            printf '%s  %s\n' "$h" "$p"
        done <"$tmp/paths"
    } >"$tmp/manifest"
    if grep -q '^REFUSED$' "$tmp/manifest"; then
        echo "engine_surface: manifest not written, resolve the paths above first" >&2
        exit 1
    fi
    mv "$tmp/manifest" "$manifest"
    n=$(grep -cvE '^[[:space:]]*(#|$)' "$manifest" || true)
    echo "pincheck: wrote $manifest ($n paths at $(git -C "$engine" rev-parse --short HEAD 2>/dev/null || echo '?'))"
    exit 0
fi

# --check
if [ ! -f "$manifest" ]; then
    echo "pincheck: FAIL, no manifest at $manifest (create it with --update)"
    exit 1
fi

bad=0
changed=0
n=0

# A bump that moved ENGINE_COMMIT and left the manifest alone is the failure this
# catches: every hash below would then still match, and would be describing a
# commit nobody claimed. Cheap, and it cannot be got right by accident.
stamped=$(sed -n 's|^#[[:space:]]*pin:[[:space:]]*||p' "$manifest" | head -1 | tr -d '[:space:]')
if [ -n "$pin" ] && [ "$stamped" != "$pin" ]; then
    bad=1
    echo "  the manifest was written for a different pin than ENGINE_COMMIT names:"
    echo "        manifest: ${stamped:-none}"
    echo "        pin:      $pin"
    echo "        Re-verify the fused build, then engine_surface.sh --update."
fi

grep -vE '^[[:space:]]*(#|$)' "$manifest" >"$tmp/entries" || true
while read -r want path; do
    [ -n "$path" ] || continue
    n=$((n + 1))
    got=$(surface_hash "$path")
    [ "$got" = "$want" ] && continue
    changed=$((changed + 1))
    bad=1
    case $got in
    GONE) printf ' %-46s GONE, the engine no longer has this file\n' "$path" ;;
    BASE-GONE) printf '  %-46s the engine own pc/patches diff no longer applies\n' "$path" ;;
    *) printf '  %-46s CHANGED\n' "$path" ;;
    esac
    if [ -n "$pin" ] && git -C "$engine" cat-file -e "$pin" 2>/dev/null; then
        git -C "$engine" log --oneline "$pin"..HEAD -- "$path" 2>/dev/null |
            head -4 | sed 's/^/        /'
    fi
done <"$tmp/entries"

required_paths | sort -u >"$tmp/required"
manifest_paths | sort -u >"$tmp/listed"
missing=$(comm -23 "$tmp/required" "$tmp/listed" || true)
if [ -n "$missing" ]; then
    bad=1
    echo "  the manifest does not cover everything it must:"
    echo "$missing" | sed 's/^/        /'
fi

unres=$(unresolved_includes)
if [ -n "$unres" ]; then
    echo "  note: included by a mod source and not a committed engine path --"
    echo "$unres" | sed 's/^/        /'
fi

if [ "$bad" -eq 0 ]; then
    echo "pincheck: ok ($n engine paths unchanged since the pin was verified)"
    exit 0
fi

echo "pincheck: $changed of $n engine paths this build depends on have moved since"
echo "  the pin was verified. Read each diff above before trusting a fused build,"
echo "  and see mmo/mods/openmmo/README.md, 'When the pin moves', for what a bump"
echo "  has to prove. Once verified: mmo/tools/engine_surface.sh --update."
exit 1
