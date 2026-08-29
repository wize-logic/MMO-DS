#!/usr/bin/env bash
# The three libraries the Windows cross build links against.
#
#   tools/win_deps.sh [destdir]        default: mmo/build/win/deps
set -euo pipefail

MINGW=${MINGW:-i686-w64-mingw32}
# The same compiler Makefile.win pins, for the one dependency built here:
# configure resolves $MINGW-gcc off $PATH, where brew's UCRT-linking mingw
# can shadow the msvcrt one, and a freetype built that way drags the whole
# window onto a C library Windows XP does not have.
MINGW_CC=${MINGW_CC:-/usr/bin/$MINGW-gcc-win32}
DEST=${1:-$(cd "$(dirname "$0")/.." && pwd)/build/win/deps}
CACHE=$DEST/cache

SDL2_VER=2.32.10
RAYLIB_VER=6.0
FREETYPE_VER=2.13.3

mkdir -p "$DEST" "$CACHE"

say() { printf 'win-deps: %s\n' "$*"; }

fetch() {  # fetch <url> <file>
    [ -f "$CACHE/$2" ] && return 0
    say "fetching $2"
    curl -fsSL --retry 3 -o "$CACHE/$2.part" "$1"
    mv "$CACHE/$2.part" "$CACHE/$2"
}

# ------------------------------------------------------------------ SDL2
sdl2() {
    local out=$DEST/sdl2
    if [ -n "${SDL2_MINGW:-}" ]; then
        say "SDL2: using SDL2_MINGW=$SDL2_MINGW"
        printf '%s\n' "$SDL2_MINGW" > "$DEST/.sdl2-prefix"
        return 0
    fi
    local port=${ENGINE:-$(cd "$(dirname "$0")/../.." && pwd)/engine/pokeplatinum}/build/sdl2-mingw/SDL2-$SDL2_VER/$MINGW
    if [ -d "$port" ]; then
        say "SDL2: reusing the port's tree at $port"
        printf '%s\n' "$port" > "$DEST/.sdl2-prefix"
        return 0
    fi
    if [ ! -d "$out/$MINGW" ]; then
        fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/SDL2-devel-$SDL2_VER-mingw.tar.gz" \
              "SDL2-devel-$SDL2_VER-mingw.tar.gz"
        say "SDL2: unpacking"
        mkdir -p "$out"
        tar -xzf "$CACHE/SDL2-devel-$SDL2_VER-mingw.tar.gz" -C "$out" --strip-components=1
    fi
    printf '%s\n' "$out/$MINGW" > "$DEST/.sdl2-prefix"
}

# ---------------------------------------------------------------- raylib
#
# The release zip, at the same version the Linux build links (brew's 6.0), so
# the two windows are drawn by one library and raygui.h has one API to meet.
raylib() {
    local out=$DEST/raylib
    local arch=win32
    [ "$MINGW" = "x86_64-w64-mingw32" ] && arch=win64
    if [ ! -f "$out/lib/libraylib.a" ]; then
        fetch "https://github.com/raysan5/raylib/releases/download/$RAYLIB_VER/raylib-${RAYLIB_VER}_${arch}_mingw-w64.zip" \
              "raylib-${RAYLIB_VER}_${arch}_mingw-w64.zip"
        say "raylib: unpacking"
        rm -rf "$out.tmp"; mkdir -p "$out.tmp"
        unzip -q "$CACHE/raylib-${RAYLIB_VER}_${arch}_mingw-w64.zip" -d "$out.tmp"
        rm -rf "$out"
        mv "$out.tmp/raylib-${RAYLIB_VER}_${arch}_mingw-w64" "$out"
        rmdir "$out.tmp" 2>/dev/null || true
    fi
    printf '%s\n' "$out" > "$DEST/.raylib-prefix"
}

# -------------------------------------------------------------- freetype
freetype() {
    local out=$DEST/freetype
    if [ -f "$out/lib/libfreetype.a" ]; then return 0; fi
    fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VER.tar.gz" \
          "freetype-$FREETYPE_VER.tar.gz"
    say "freetype: cross-building $FREETYPE_VER (a minute or two)"
    rm -rf "$DEST/freetype-build"
    mkdir -p "$DEST/freetype-build"
    tar -xzf "$CACHE/freetype-$FREETYPE_VER.tar.gz" -C "$DEST/freetype-build" --strip-components=1
    (
        cd "$DEST/freetype-build"
        ./configure --host="$MINGW" CC="$MINGW_CC" --prefix="$out" \
            --enable-static --disable-shared \
            --with-zlib=no --with-bzip2=no --with-png=no \
            --with-harfbuzz=no --with-brotli=no >configure.log 2>&1
        make -j"$(nproc)" >build.log 2>&1
        make install >>build.log 2>&1
    )
    rm -rf "$DEST/freetype-build"
    printf '%s\n' "$out" > "$DEST/.freetype-prefix"
}

sdl2
raylib
freetype
say "ok, $DEST"
say "  SDL2     $(cat "$DEST/.sdl2-prefix")"
say "  raylib   $DEST/raylib"
say "  freetype $DEST/freetype"
