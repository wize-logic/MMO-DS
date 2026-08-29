#!/bin/sh
# The shared frame page still agrees on keys and sound.
set -eu

ROOT=${1:?usage: viewpage_test.sh <mmo-root> [build-dir] [engine-dir]}
BUILD=${2:-}
ENGINE=${3:-}

OURS="$ROOT/include/view_channel.h"
VIEWER="$ROOT/viewer/viewer.c"
PIN="$ROOT/tests/view_channel_test.c"

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "the window's frame page still matches the game's:"

if [ ! -f "$OURS" ]; then
    echo "viewpage: SKIP (no $OURS)"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# --- the window still refuses a page of the wrong size ---

# The comparison itself lives in the host seam now (src/platform.c: exact
# where the object carries its own length, page-granular where it does not),
# so what is checked here is that the window still asks, and still says the
# sentence a player can act on when the answer is no.
if grep -Fq 'mmo_shm_size_matches(&vw->page, sizeof *shm)' "$VIEWER" \
   && grep -Fq 'keys and sound sit past the picture' "$VIEWER"; then
    ok "the window still refuses a page of the wrong size"
else
    bad "the window still refuses a page of the wrong size" \
        "$VIEWER no longer compares the published page to sizeof the struct"
fi

if grep -Fq 'sizeof(struct openmmo_view_shm)' "$PIN"; then
    ok "the suite still pins the page's sizeof"
else
    bad "the suite still pins the page's sizeof" \
        "$PIN no longer names sizeof(struct openmmo_view_shm)"
fi

print_layout() { # include-dir header struct
    cat > "$tmp/lay.c" <<EOF
#include <stddef.h>
#include <stdio.h>
#include "$2"
int main(void)
{
    printf("%zu %zu %zu\\n",
           sizeof(struct $3),
           offsetof(struct $3, in_keys),
           offsetof(struct $3, audio_head));
    return 0;
}
EOF
    cc -O0 -o "$tmp/lay" "$tmp/lay.c" -I"$1" >/dev/null 2>"$tmp/cc.err" || {
        echo "compile failed"
        cat "$tmp/cc.err" >&2
        return 1
    }
    "$tmp/lay"
}

ours_lay=$(print_layout "$ROOT/include" "view_channel.h" openmmo_view_shm) || {
    bad "our page header still compiles" "cc refused include/view_channel.h"
    ours_lay="0 0 0"
}
ours_size=$(echo "$ours_lay" | awk '{print $1}')
ours_keys=$(echo "$ours_lay" | awk '{print $2}')
ours_audio=$(echo "$ours_lay" | awk '{print $3}')

if [ -n "$ours_size" ] && [ "$ours_size" -gt 0 ]; then
    ok "our page is $ours_size bytes (keys at $ours_keys, audio at $ours_audio)"
fi

hd_of() {
    sed -n "s/^#define $2 \\([0-9][0-9]*\\).*/\\1/p" "$1" | head -1
}

ours_hd=$(hd_of "$OURS" OPENMMO_VIEW_HD_MAX)
if [ -n "$ours_hd" ]; then
    ok "OPENMMO_VIEW_HD_MAX is $ours_hd"
else
    bad "OPENMMO_VIEW_HD_MAX is set" "$OURS has no OPENMMO_VIEW_HD_MAX"
    ours_hd=0
fi

# --- against the publisher ---

PUB="$ENGINE/pc/include/pc_view.h"
if [ -z "$ENGINE" ] || [ ! -f "$PUB" ]; then
    echo "  skip  publisher header (no engine checkout)"
else
    # The publisher this window meets is the fused build's, which compiles the
    # engine header with the mod's header patch staged first on the include
    # path (pc/modinclude.py). Compare against that page, not vanilla's.
    mkdir -p "$tmp/pub-inc"
    cp "$PUB" "$tmp/pub-inc/pc_view.h"
    MODPATCH="$ROOT/mods/openmmo/patches/pc/include/pc_view.h.patch"
    if [ -f "$MODPATCH" ]; then
        if patch --silent --forward --fuzz=0 "$tmp/pub-inc/pc_view.h" \
                 "$MODPATCH" >/dev/null 2>&1; then
            ok "the mod's pc_view.h patch applies to the engine header"
        else
            bad "the mod's pc_view.h patch applies to the engine header" \
                "$MODPATCH no longer applies with fuzz=0"
        fi
    fi
    PUB="$tmp/pub-inc/pc_view.h"
    theirs_hd=$(hd_of "$PUB" PC_VIEW_HD_MAX)
    if [ -n "$theirs_hd" ] && [ "$ours_hd" = "$theirs_hd" ]; then
        ok "PC_VIEW_HD_MAX is also $theirs_hd"
    else
        bad "PC_VIEW_HD_MAX matches OPENMMO_VIEW_HD_MAX" \
            "window $ours_hd, game ${theirs_hd:-unset}, keys and sound sit past the picture"
    fi

    theirs_lay=$(print_layout "$tmp/pub-inc" "pc_view.h" pc_view_shm) || {
        bad "the publisher's page header still compiles" "cc refused pc/include/pc_view.h"
        theirs_lay="0 0 0"
    }
    theirs_size=$(echo "$theirs_lay" | awk '{print $1}')
    theirs_keys=$(echo "$theirs_lay" | awk '{print $2}')
    theirs_audio=$(echo "$theirs_lay" | awk '{print $3}')

    if [ "$ours_size" = "$theirs_size" ] \
       && [ "$ours_keys" = "$theirs_keys" ] \
       && [ "$ours_audio" = "$theirs_audio" ]; then
        ok "both pages are $theirs_size bytes at the same key and audio offsets"
    else
        bad "both pages have the same size and key/audio offsets" \
            "window $ours_size/$ours_keys/$ours_audio, game $theirs_size/$theirs_keys/$theirs_audio"
    fi

    # The failure this file exists for: a publisher whose pixel arrays
    # grew (or shrank) without a version bump.
    fake_hd=2
    [ "$theirs_hd" = "2" ] && fake_hd=4
    sed "s/^#define PC_VIEW_HD_MAX ${theirs_hd}u/#define PC_VIEW_HD_MAX ${fake_hd}u/" \
        "$PUB" > "$tmp/pc_view.h"
    mkdir -p "$tmp/inc"
    cp "$tmp/pc_view.h" "$tmp/inc/pc_view.h"
    fake_lay=$(print_layout "$tmp/inc" "pc_view.h" pc_view_shm) || fake_lay="0 0 0"
    fake_size=$(echo "$fake_lay" | awk '{print $1}')
    if [ "$fake_size" != "$ours_size" ] && [ "$fake_size" != 0 ]; then
        ok "a publisher at HD_MAX=$fake_hd is a different page ($fake_size bytes)"
    else
        bad "a publisher at HD_MAX=$fake_hd is a different page" \
            "mutated header still reports $fake_size bytes"
    fi
fi

# A leftover page from a drifted game is the thing a person meets.
if [ -d /dev/shm ]; then
    live=0
    drift=0
    for f in /dev/shm/openmmo /dev/shm/openmmo-*; do
        [ -f "$f" ] || continue
        case $f in *.text|*.status) continue ;; esac
        magic=$(od -An -N4 -tx1 "$f" 2>/dev/null | tr -d ' \n')
        [ "$magic" = "564c5050" ] || [ "$magic" = "50504c56" ] || continue
        live=$((live + 1))
        sz=$(wc -c < "$f" | tr -d ' ')
        if [ "$sz" != "$ours_size" ]; then
            echo "       $f is $sz bytes, window expects $ours_size"
            drift=$((drift + 1))
        fi
    done
    if [ "$live" -eq 0 ]; then
        echo "  skip  no live frame page under /dev/shm"
    elif [ "$drift" -eq 0 ]; then
        ok "every live frame page is $ours_size bytes ($live)"
    else
        bad "every live frame page is $ours_size bytes" \
            "$drift of $live published page(s) would swallow keys and sound"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "viewpage: FAILED"
    exit 1
fi
echo "viewpage: all checks passed"
exit 0
