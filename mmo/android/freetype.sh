#!/usr/bin/env bash
# Build the static FreeType the app links, for one Android ABI.
#
#   ./mmo/android/freetype.sh                 # armeabi-v7a -> ~/.local/opt/freetype-android
#   ./mmo/android/freetype.sh x86             # x86         -> ~/.local/opt/freetype-android-x86
set -euo pipefail

ABI="${1:-armeabi-v7a}"
NDK="${NDK:-$HOME/.local/opt/android-ndk-r27c}"
API="${ANDROID_API:-26}"
VER=2.13.3
SHA=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289

case "$ABI" in
armeabi-v7a) HOST=armv7a-linux-androideabi; CCNAME=armv7a-linux-androideabi; SUFFIX="" ;;
x86)         HOST=i686-linux-android;       CCNAME=i686-linux-android;       SUFFIX="-x86" ;;
*) echo "freetype.sh: ABI must be armeabi-v7a or x86, not '$ABI'" >&2; exit 2 ;;
esac

PREFIX="${PREFIX:-$HOME/.local/opt/freetype-android$SUFFIX}"
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$BIN/${CCNAME}${API}-clang"
[ -x "$CC" ] || { echo "freetype.sh: no $CC; set NDK" >&2; exit 1; }

WORK="${WORK:-$(dirname "$0")/../build/freetype-$ABI}"
mkdir -p "$WORK"
cd "$WORK"
TAR="freetype-$VER.tar.xz"
if [ ! -f "$TAR" ]; then
    curl -sSL -o "$TAR" "https://download.savannah.gnu.org/releases/freetype/$TAR"
fi
echo "$SHA  $TAR" | sha256sum -c - >/dev/null || {
    echo "freetype.sh: $TAR does not match the pinned sha256" >&2; exit 1; }
rm -rf "freetype-$VER"
tar -xf "$TAR"
cd "freetype-$VER"

# --with-*=no for every optional library, exactly as the armeabi-v7a copy was
# built. --host makes configure cross; CC names the NDK's per-API driver so
# no sysroot flags are needed.
./configure --host="$HOST" --prefix="$PREFIX" \
    --disable-shared --enable-static \
    --with-zlib=no --with-bzip2=no --with-png=no --with-harfbuzz=no \
    --with-brotli=no \
    CC="$CC" AR="$BIN/llvm-ar" RANLIB="$BIN/llvm-ranlib" \
    CFLAGS="-O2 -fPIC" >configure.log 2>&1 || {
    tail -20 configure.log >&2; exit 1; }
make -j"$(nproc)" >make.log 2>&1 || { tail -20 make.log >&2; exit 1; }
make install >install.log 2>&1
echo "freetype.sh: $ABI -> $PREFIX/lib/libfreetype.a"
"$BIN/llvm-readelf" -h "$PREFIX/lib/libfreetype.a" 2>/dev/null | grep -m1 'Machine:'
