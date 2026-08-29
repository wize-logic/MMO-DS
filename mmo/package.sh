#!/usr/bin/env bash
#
# mmo/package.sh, the game as one directory a person can run.
#
# Playing this game means running two programs that meet in a shared page, and
# choosing what they do means a third in front of them. In a built tree those
# live at three different paths and `play.sh` knows all of them; on someone
# else's machine there is no tree and no Makefile, so this lays them out in the
# one arrangement where each program's own default lookup finds the next:
#
#   openmmo/                   plain, no version: the folder outlives every
#                              auto-update, so the name must not claim one,
#                              the archive's name and revision.txt carry it
#     openmmo                  run this          (Windows: OpenMMO.cmd)
#     bin/openmmo-launch       the front door, finds the two below beside it
#     bin/openmmo-view         the window
#     bin/pokeplatinum         the game
#     bin/SDL2.dll             Windows only, the one library a -static exe
#                              cannot absorb, and it goes beside the window
#     res/launcher/            the two pictures the launcher draws
#     rom/                     where the cartridge image goes, empty here
#     mods/                    where content packages go, empty here
#     revision.txt             the install revision, which the launcher reads
#
# `bin/pokeplatinum` resolves its assets at `<its own dir>/../rom/` when $PC_ROM
# is unset, which in this layout is the `rom/` directory beside it, so the
# game and the launcher name one place rather than two.
#
# One script, two hosts, the same arrangement Makefile.win has with Makefile:
# the layout, the allowlist, the hashes and the refusals are the package, and
# they do not get a second copy that drifts. `--host` changes the four things
# that genuinely differ, the `.exe` suffix, the DLL beside the window, the
# front door a person double-clicks, and whether the archive is a tarball or a
# zip, and nothing else.
#
# NOTHING THIRD-PARTY AS GAME DATA GOES IN. The cartridge image, every NARC
# member a porter filled from one, and any species face taken from a commercial
# ROM are the player's; this packages programs and launcher art we hold the
# rights to. The design notes is the allowlist. The folder is a local build
# (`make package`); this script does not publish it.
#
# Nothing in the folder that a program in the folder does not read. No README,
# no manifest, no credits file, no licence text, and not the headless client
# either: a release is the game, the two programs that put it on a screen, and
# the handful of files those three open. Every other file was one a player had
# to be told to ignore. What a player must supply, what the object code is
# licensed under and what is deliberately absent are the design notes, and in
# full in the two git trees the programs are built from, which is a local
# build's position anyway, since the folder is not the copy anyone is conveyed.
#
# The hashes do not travel inside the folder. They are written beside the
# archive as `openmmo-<version>-<platform>.sha256`, so what a tree is checked
# against is not a file that tree carries and could be rewritten with.
#
#   mmo/package.sh                        # build/dist/openmmo/ + versioned .tar.gz
#   mmo/package.sh --host windows         # build/win/dist/openmmo/ + versioned .zip
#   mmo/package.sh --out DIR              # somewhere else
#   mmo/package.sh --version V            # name it yourself (default: git describe)
#   mmo/package.sh --build DIR            # take the binaries from another build tree
#   mmo/package.sh --archive zip          # override the host's own archive format
#   mmo/package.sh --no-archive           # leave the directory, skip the archive
#   mmo/package.sh --strip                # drop the debug info from the copies
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD=""
OUT=""
VERSION="${VERSION:-}"
HOST="${HOST:-linux}"
# Empty means "whatever this host packages in", which is what `make package`
# writes. dist.sh asks for zip on both, because handing someone two releases
# means handing them two files that open the same way.
FORMAT="${FORMAT:-}"
STRIP_DEBUG=0

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)       HOST="$2"; shift 2;;
        --build)      BUILD="$2"; shift 2;;
        --out)        OUT="$2"; shift 2;;
        --version)    VERSION="$2"; shift 2;;
        --archive)    FORMAT="$2"; shift 2;;
        --no-archive) FORMAT="none"; shift;;
        --strip)      STRIP_DEBUG=1; shift;;
        -h|--help)    usage; exit 0;;
        *) printf 'package: unknown option %s (try --help)\n' "$1" >&2; exit 2;;
    esac
done

# The host is named, never sniffed. A package is the one artifact whose shape
# nobody checks before handing it over, so which one is being built is a thing
# the caller said out loud. Three values follow from it: what a program is
# called, what the archive is called, and the `make` that would have built the
# thing that is missing.
case "$HOST" in
    linux)   EXE="";     PLATFORM="linux-x86_64"; MK="make -C mmo";                 NATIVE=tar.gz;
             STRIP=strip;;
    windows) EXE=".exe"; PLATFORM="windows-x86";  MK="make -C mmo -f Makefile.win"; NATIVE=zip;
             STRIP=i686-w64-mingw32-strip;;
    *) printf 'package: --host must be linux or windows, not %s\n' "$HOST" >&2; exit 2;;
esac
FORMAT="${FORMAT:-$NATIVE}"
case "$FORMAT" in
    tar.gz|zip|none) ;;
    *) printf 'package: --archive must be zip, tar.gz or none, not %s\n' "$FORMAT" >&2; exit 2;;
esac

# Each host's build tree, because Makefile.win's BUILD is build/win and a
# person typing this by hand should not have to remember that.
if [[ -z "$BUILD" ]]; then
    if [[ "$HOST" == windows ]]; then BUILD="$ROOT/build/win"; else BUILD="$ROOT/build"; fi
fi
OUT="${OUT:-$BUILD/dist}"

# `-dirty` asks about the tree that made these bytes, and no decomp submodule
# contributes one of them. `git describe --dirty` disagrees: it calls the tree
# dirty for a gitlink the engine work moved ahead of, so a clean checkout names
# every archive `-dirty`. Ask git status what changed instead, submodules aside.
if [[ -z "$VERSION" ]]; then
    VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null)"
    VERSION="${VERSION:-dev}"
    if [[ -n "$(git -C "$ROOT" status --porcelain --ignore-submodules=all 2>/dev/null)" ]]; then
        VERSION="$VERSION-dirty"
    fi
fi

# What goes in, and the target that builds each. Named one by one: a package
# missing the game is worth refusing by name, not shipping without it.
#   <source under $BUILD>  <path in the package>  <what builds it>
INPUTS=(
    "fused/pokeplatinum$EXE|bin/pokeplatinum$EXE|$MK fused"
    "openmmo-view$EXE|bin/openmmo-view$EXE|$MK viewer"
    "openmmo-launch$EXE|bin/openmmo-launch$EXE|$MK launcher"
)
# SDL2.dll is a library, not game data, and the design notes lists it: it is the one
# thing `-static` cannot absorb and the one thing a Windows machine does not
# already have. It goes in bin/ because that is where the window is.
if [[ "$HOST" == windows ]]; then
    INPUTS+=("SDL2.dll|bin/SDL2.dll|$MK viewer")
fi
# And raylib is the same thing on the other host: the front door links it
# shared, its soname is not one a Linux machine already has, and without it in
# the package the launcher stops at the loader with no window and one line of
# terminal.
if [[ "$HOST" == linux ]]; then
    # Whatever soname the launcher was linked against, which the Makefile
    # staged beside it. Falling back to a name rather than refusing here, so
    # an incomplete tree is reported by the loop below with everything else
    # it is missing, a release with no game is the louder failure and has
    # to be the one a person reads first.
    raylib_so="$(cd "$BUILD" 2>/dev/null && ls libraylib.so.* 2>/dev/null | head -1)"
    raylib_so="${raylib_so:-libraylib.so.600}"
    INPUTS+=("$raylib_so|bin/$raylib_so|$MK launcher")
fi

missing=0
for spec in "${INPUTS[@]}"; do
    IFS='|' read -r src _dst how <<<"$spec"
    if [[ ! -f "$BUILD/$src" ]]; then
        printf 'package: no %s -- run `%s`\n' "$BUILD/$src" "$how" >&2
        missing=1
    fi
done
[[ "$missing" -eq 0 ]] || exit 1

# `rom/` and `mods/` are made here and stay empty: they are the two
# directories the player fills, and each program already looks in its own one.
STAGE="$OUT/openmmo"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/rom" "$STAGE/mods" "$STAGE/logs" \
         "$STAGE/res/launcher" || exit 1
cp "$ROOT/res/launcher/bg.png" "$STAGE/res/launcher/bg.png" || exit 1
cp "$ROOT/res/launcher/wordmark.png" "$STAGE/res/launcher/wordmark.png" || exit 1

# --strip-debug and not -s.
for spec in "${INPUTS[@]}"; do
    IFS='|' read -r src dst _how <<<"$spec"
    cp "$BUILD/$src" "$STAGE/$dst" || exit 1
    # Writable as well as runnable: a copy of a library the package manager
    # installed read-only arrives read-only, and strip cannot open it.
    chmod u+w,+x "$STAGE/$dst"
    if [[ "$STRIP_DEBUG" -eq 1 && "$dst" != */SDL2.dll ]]; then
        if ! command -v "$STRIP" >/dev/null 2>&1; then
            printf 'package: no %s, cannot strip %s\n' "$STRIP" "$dst" >&2
            exit 1
        fi
        "$STRIP" --strip-debug "$STAGE/$dst" || exit 1
    fi
done

# The one text file this script writes for a person to open is the front door,
# and a .cmd is parsed a line at a time by a program that has wanted CRLF since
# Dos. revision.txt is machine-read and stays as it is.
crlf() {
    [[ "$HOST" == windows ]] || return 0
    sed -i 's/$/\r/' "$1"
}

# The launcher's feed reads this as the install revision. Absent or unparseable
# means "unknown", which the feed treats as needing a full update. The number
# is the commit count on this tree, not git-describe: the reader is strtol.
revcount="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || true)"
if [[ ! "$revcount" =~ ^[1-9][0-9]*$ ]]; then
    printf 'package: cannot count commits for revision.txt\n' >&2
    exit 1
fi
printf '%s\n' "$revcount" > "$STAGE/revision.txt" || exit 1

# The front door: one file, at the top, whose whole job is to start bin/ with
# no arguments a person has to know. Both spellings pass their own arguments
# through, so --play and --help work from either.
if [[ "$HOST" == windows ]]; then
    FRONTDOOR="OpenMMO.cmd"
    cat > "$STAGE/$FRONTDOOR" <<'EOF'
@echo off
rem The front door, double-click it. Everything it starts is beside it in
rem bin\; the cartridge image goes in rom\ and is not supplied. --help lists
rem the rest. `start` is what lets this console box close immediately instead
rem of sitting behind the game for the whole session.
rem
rem PC_MODS_DIR is set here and not in the launcher's settings because it has to
rem be ABSOLUTE, the engine resolves a relative one from the game's working
rem directory, and a shipped settings file cannot know where it was unpacked.
rem %~dp0 does know. A mods-dir set in the launcher still wins over this.
set "PC_MODS_DIR=%~dp0mods"
rem Say what is wrong before Windows says it worse. Double-clicking this from
rem inside the .zip runs it out of a temporary folder that holds only this
rem file, so bin\ is not there and Windows answers with a "cannot find" box
rem naming a path the player never typed. The same box appears when a virus
rem scanner has quarantined the game. Both are worth a sentence.
if not exist "%~dp0bin\openmmo-launch.exe" (
    echo.
    echo   OpenMMO is not unpacked.
    echo.
    echo   bin\openmmo-launch.exe is not beside this file. If you started
    echo   this from inside the .zip, extract the whole openmmo folder
    echo   somewhere first and run OpenMMO.cmd from there.
    echo.
    echo   If you did extract it, check whether your virus scanner has
    echo   removed bin\openmmo-launch.exe or bin\pokeplatinum.exe.
    echo.
    pause
    exit /b 1
)
start "OpenMMO" "%~dp0bin\openmmo-launch.exe" %*
EOF
else
    FRONTDOOR="openmmo"
    cat > "$STAGE/$FRONTDOOR" <<'EOF'
#!/bin/sh
# The front door. Everything it starts is beside it in bin/; the cartridge
# image is in rom/ and is not supplied. --help lists the rest.
dir=$(cd "$(dirname "$0")" && pwd)
PC_MODS_DIR="$dir/mods"; export PC_MODS_DIR
exec "$dir/bin/openmmo-launch" "$@"
EOF
fi
chmod +x "$STAGE/$FRONTDOOR"
crlf "$STAGE/$FRONTDOOR"

engine_commit="$(grep -vE '^[[:space:]]*(#|$)' "$ROOT/ENGINE_COMMIT" 2>/dev/null | head -1 | tr -d '[:space:]')"

# The check that the package is what this script says it is. An exact list, not
# a rule of thumb: a stray file is how game data gets into a release nobody
# meant to put it in.
EXPECTED=(
    "$FRONTDOOR"
    "bin/openmmo-launch$EXE"
    "bin/openmmo-view$EXE"
    "bin/pokeplatinum$EXE"
    "res/launcher/bg.png"
    "res/launcher/wordmark.png"
    "revision.txt"
)
[[ "$HOST" == windows ]] && EXPECTED+=("bin/SDL2.dll")
# Named by the soname the launcher was linked against, the same string
# INPUTS carried it in under, so the guard still checks a fixed list
# rather than waving a whole directory through.
[[ "$HOST" == linux ]] && EXPECTED+=("bin/$raylib_so")

actual="$(cd "$STAGE" && find . -type f -printf '%P\n' | sort)"
wanted="$(printf '%s\n' "${EXPECTED[@]}" | sort)"
if [[ "$actual" != "$wanted" ]]; then
    printf 'package: *** the staged tree is not what this script packages ***\n' >&2
    diff <(printf '%s\n' "$wanted") <(printf '%s\n' "$actual") |
        sed 's/^</  missing: /; s/^>/  unexpected: /' >&2
    exit 1
fi

# The two the player fills are directories and not files, so the list above
# cannot speak for them: empty is the claim, and an empty directory is the one
# state `find -type f` reports the same way as a missing one.
for d in rom mods; do
    if [[ ! -d "$STAGE/$d" ]]; then
        printf 'package: no %s/ in the package, the player has nowhere to put theirs\n' "$d" >&2
        exit 1
    fi
    if [[ -n "$(ls -A "$STAGE/$d")" ]]; then
        printf 'package: %s/ is not empty: %s\n' "$d" "$(ls -A "$STAGE/$d" | tr '\n' ' ')" >&2
        exit 1
    fi
done

# The hashes, beside the archive rather than in the folder. A manifest a tree
# carries is rewritten by whatever rewrote the tree; this one is what the folder
# is checked against, so it is written where it does not travel with it.
SUMS="$OUT/openmmo-$VERSION-$PLATFORM.sha256"
{
    printf 'openmmo %s\n' "$VERSION"
    printf 'engine %s\n' "${engine_commit:-unknown}"
    printf 'host %s\n' "$PLATFORM"
    printf '\n'
    ( cd "$STAGE" && find . -type f -printf '%P\n' | sort |
      while read -r f; do printf '%s  %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" "$f"; done )
} > "$SUMS" || exit 1

printf 'package: %s\n' "$STAGE"
printf 'package: %s files, %s\n' "$(printf '%s\n' "${EXPECTED[@]}" | wc -l)" \
       "$(du -sh "$STAGE" | cut -f1)"
printf 'package: %s\n' "$SUMS"

# The archive is the directory and nothing else, by default in the form the
# host's own file manager opens with no second program installed: tar.gz on
# Linux, and a zip on Windows, where Explorer unpacks one by double-clicking it
# and unpacks nothing else. --archive overrides that, because a zip is also the
# one format both desktops open, which is what dist.sh hands over.
case "$FORMAT" in
    zip)
        AR="$OUT/openmmo-$VERSION-$PLATFORM.zip"
        rm -f "$AR"
        # -X so no uid, gid or Unix extra field travels: what Explorer gets
        # is names, times and bytes. The execute bit is not one of those,
        # it lives in the external attributes, which -X keeps, so a Linux zip
        # still unpacks a runnable front door. -9 because the 90 MB game
        # compresses to under half, and this is written once and copied many
        # times.
        ( cd "$OUT" && zip -qrX9 "$AR" "openmmo" ) || exit 1
        ;;
    tar.gz)
        AR="$OUT/openmmo-$VERSION-$PLATFORM.tar.gz"
        rm -f "$AR"
        tar -czf "$AR" -C "$OUT" "openmmo" || exit 1
        ;;
    none) AR="";;
esac
[[ -z "$AR" ]] || printf 'package: %s (%s)\n' "$AR" "$(du -h "$AR" | cut -f1)"
