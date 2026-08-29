#!/bin/sh
# What a release contains, and what it refuses to contain.
#
# The release is the one artifact nobody looks inside before handing it to
# someone: a stray file in it is shipped, not noticed. So the claim under test
# is an exact list, seven files on Linux, eight on Windows, and nothing
# else, checked here against a list written independently of the one
# package.sh stages from, so the two drifting apart is a failure rather than a
# rename that agrees with itself. The design notes is the class allowlist; this
# file is the names, one list per host.
#
# Every one of those seven is a program or a file a program in the folder
# opens. Nothing else is in it: no README, no manifest, no credits, no licence
# text, no headless client. That is the second claim, and it is checked by
# name, because each of those left a release one at a time and a re-added one
# would otherwise only show up as a number that moved.
#
# The third claim is that the release carries no game data. That cannot be
# proved by looking at what a correct run produced; it is proved by the empty
# `rom/` and by nothing in the tree being the size or the name of a cartridge,
# a NARC member, or a porter output.
#
# Stub binaries, not real ones: the layout, the refusals and the manifest are
# what this checks, and requiring a fused build (and therefore the engine
# checkout) to check them would put this behind a tree most machines running
# `make test` do not have.
#
# Usage: package_test.sh <mmo-root>
set -eu

ROOT=${1:?usage: package_test.sh <mmo-root>}
PKG="$ROOT/package.sh"

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/openmmo-package-XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM

BUILD="$TMP/build"
OUT="$TMP/out"
mkdir -p "$BUILD/fused"

echo "package.sh builds a release from a built tree:"

# An incomplete tree is refused by name, with the target that fixes it. A
# release missing the game is the failure worth being loud about.
if out=$("$PKG" --build "$BUILD" --out "$OUT" --version t --no-archive 2>&1); then
    bad "an empty build tree was packaged anyway"
else
    case "$out" in
        *"fused/pokeplatinum"*"make -C mmo fused"*)
            ok "a missing game is refused by name, with the target that builds it" ;;
        *) bad "the refusal did not name the game and its target: $out" ;;
    esac
fi

for f in fused/pokeplatinum openmmo-view openmmo-launch; do
    printf 'stub\n' > "$BUILD/$f"
    chmod +x "$BUILD/$f"
done
# The launcher links raylib shared and the soname is not one a Linux machine
# already has, so the package carries it and package.sh refuses without it.
# The Makefile stages it beside the launcher; this stands in for that.
printf 'stub\n' > "$BUILD/libraylib.so.600"

if ! out=$("$PKG" --build "$BUILD" --out "$OUT" --version t --no-archive 2>&1); then
    bad "a complete build tree was not packaged: $out"
    echo "package: $fails check(s) FAILED"
    exit 1
fi
ok "a complete build tree is packaged"

# The folder is plain openmmo whatever the version: an auto-updating install
# keeps its folder, so the name must not claim a revision.
STAGE="$OUT/openmmo"

# The list, written here rather than read from the script that produced it.
WANTED=$(printf '%s\n' \
    revision.txt \
    bin/openmmo-launch bin/openmmo-view bin/pokeplatinum \
    bin/libraylib.so.600 \
    openmmo res/launcher/bg.png res/launcher/wordmark.png | sort)
ACTUAL=$(cd "$STAGE" && find . -type f -printf '%P\n' | sort)
if [ "$WANTED" = "$ACTUAL" ]; then
    ok "the release holds exactly the files a release holds"
else
    bad "the release is not the expected set of files"
    printf '%s\n' "$WANTED" > "$TMP/wanted"
    printf '%s\n' "$ACTUAL" > "$TMP/actual"
    diff -u "$TMP/wanted" "$TMP/actual" | sed 's/^/    /' || true
fi

# No game data. `rom/` and `mods/` are where a player puts theirs, they are in
# the folder so that there is somewhere to put it, and both leave here empty,
# and nothing anywhere in the tree is a cartridge by name or by size. `logs/`
# is the same shape from the other side: the programs fill it, and a release
# that shipped one log in it would be shipping somebody else's session.
for d in rom mods logs; do
    if [ ! -d "$STAGE/$d" ]; then
        bad "$d/ is not in the release, a player has nowhere to put theirs"
    elif [ -z "$(ls -A "$STAGE/$d")" ]; then
        ok "$d/ is in the release and ships empty"
    else
        bad "$d/ holds something: $(ls -A "$STAGE/$d" | tr '\n' ' ')"
    fi
done
if find "$STAGE" \( -name '*.nds' -o -name '*.narc' -o -name '*.cooked' \
        -o -name 'mod.toml' -o -size +64M \) | grep -q .; then
    bad "the release carries cartridge data, a NARC, a cooked overlay, or a package"
else
    ok "and nothing in the release is a cartridge, a NARC, a cooked overlay or a package"
fi

# revision.txt is the commit count, a positive integer the front door can parse.
if [ -f "$STAGE/revision.txt" ]; then
    got=$(tr -d '\n' < "$STAGE/revision.txt")
    want=$(git -C "$ROOT" rev-list --count HEAD)
    case "$got" in
        ''|*[!0-9]*) bad "revision.txt is not an integer: $got" ;;
        0) bad "revision.txt is 0, which the feed treats as unknown" ;;
        "$want") ok "revision.txt is the commit count ($got)" ;;
        *) bad "revision.txt is $got, git rev-list --count HEAD is $want" ;;
    esac
else
    bad "revision.txt is missing"
fi

# Nothing in the folder that a program in the folder does not open. Named one
# by one rather than left to the count above: each of these was taken out on
# its own, and a re-added one should say which it was.
byname=1
for f in LICENSE LICENSE.GPL-3.0 README.md MANIFEST CREDITS \
         res/launcher/CREDITS mods/PUT-PACKAGES-HERE.txt \
         rom/PUT-THE-ROM-HERE.txt bin/openmmo-client; do
    [ -e "$STAGE/$f" ] && { bad "$f is back in the package"; byname=0; }
done
[ "$byname" -eq 1 ] && ok "no licence, readme, manifest, credits, note or headless client travels"

# The hashes are beside the archive, not inside the folder, and they cover
# every file in it.
SUMS="$OUT/openmmo-t-linux-x86_64.sha256"
if [ ! -f "$SUMS" ]; then
    bad "no openmmo-t-linux-x86_64.sha256 beside the release"
else
    mfails=0
    for f in $ACTUAL; do
        want=$(sha256sum "$STAGE/$f" | cut -d' ' -f1)
        grep -qF "$want  $f" "$SUMS" || mfails=$((mfails + 1))
    done
    [ "$mfails" -eq 0 ] && ok "the sha256 file beside the release hashes every file in it" \
                        || bad "$mfails file(s) missing or wrong in the sha256 file"
fi

# The one thing a person is told to run has to start the front door.
if [ -x "$STAGE/openmmo" ] && grep -q 'bin/openmmo-launch' "$STAGE/openmmo"; then
    ok "the run script is executable and starts the launcher"
else
    bad "the run script does not start the launcher"
fi

# What is not in the box is said in the tree instead, since the box no longer
# says anything: the design notes is the allowlist, and it is the page a reader is
# sent to for the ROM, the licences and the local-build position.
if [ -f "$ROOT/PACKAGE.md" ]; then
    allow_says=1
    for phrase in 'may carry' 'Gen-5' 'pokeplatinum.us.nds' 'AGPLv3' 'GPLv3' 'does not publish'; do
        grep -qF "$phrase" "$ROOT/PACKAGE.md" || { bad "PACKAGE.md never mentions $phrase"; allow_says=0; }
    done
    [ "$allow_says" -eq 1 ] && ok "PACKAGE.md is the allowlist and names the ROM, both licences and the Gen-5 refuse"
fi

# The archive is the directory, not a rearrangement of it.
"$PKG" --build "$BUILD" --out "$OUT" --version t >/dev/null 2>&1 || bad "the tarball was not written"
mkdir -p "$TMP/unpacked"
if tar xzf "$OUT/openmmo-t-linux-x86_64.tar.gz" -C "$TMP/unpacked" 2>/dev/null; then
    UNPACKED=$(cd "$TMP/unpacked/openmmo" && find . -type f -printf '%P\n' | sort)
    [ "$UNPACKED" = "$WANTED" ] && ok "the tarball unpacks to that same release" \
                               || bad "the tarball is not what was staged"
    # rom/, mods/ and logs/ hold no file now, so the archive is the only thing
    # keeping them: an archiver that drops empty directories leaves a player
    # with nowhere to put the cartridge and a game that looks for one anyway.
    # logs/ is the one the programs would remake on their own, and it is held
    # to the same rule so that a fresh unzip already shows where to look.
    if [ -d "$TMP/unpacked/openmmo/rom" ] && [ -d "$TMP/unpacked/openmmo/mods" ] &&
       [ -d "$TMP/unpacked/openmmo/logs" ]; then
        ok "and the three empty directories survive it"
    else
        bad "the tarball dropped rom/, mods/ or logs/ for being empty"
    fi
else
    bad "the tarball did not unpack"
fi

# ---------------------------------------------------------------------------
# The same script, the other host.

echo
echo "package.sh --host windows builds the same release for the other host:"

WBUILD="$TMP/wbuild"
WOUT="$TMP/wout"
mkdir -p "$WBUILD/fused"
for f in fused/pokeplatinum.exe openmmo-view.exe openmmo-launch.exe; do
    printf 'stub\n' > "$WBUILD/$f"
done

# SDL2.dll is in the input list and not an afterthought: a Windows release
# without it is four programs and a window that will not open.
if out=$("$PKG" --host windows --build "$WBUILD" --out "$WOUT" --version t --no-archive 2>&1); then
    bad "a Windows tree with no SDL2.dll was packaged anyway"
else
    case "$out" in
        *"SDL2.dll"*"Makefile.win viewer"*)
            ok "a missing SDL2.dll is refused by name, with the target that copies it" ;;
        *) bad "the refusal did not name SDL2.dll and its target: $out" ;;
    esac
fi
printf 'stub\n' > "$WBUILD/SDL2.dll"

if ! out=$("$PKG" --host windows --build "$WBUILD" --out "$WOUT" --version t --no-archive 2>&1); then
    bad "a complete Windows build tree was not packaged: $out"
    echo "package: $fails check(s) FAILED"
    exit 1
fi
ok "a complete Windows build tree is packaged"

WSTAGE="$WOUT/openmmo"

# The list, written here rather than read from the script that produced it,
# the same rule the Linux list above is held to.
WWANTED=$(printf '%s\n' \
    revision.txt \
    bin/SDL2.dll bin/openmmo-launch.exe \
    bin/openmmo-view.exe bin/pokeplatinum.exe \
    OpenMMO.cmd res/launcher/bg.png res/launcher/wordmark.png | sort)
WACTUAL=$(cd "$WSTAGE" && find . -type f -printf '%P\n' | sort)
if [ "$WWANTED" = "$WACTUAL" ]; then
    ok "the Windows release holds exactly the files a Windows release holds"
else
    bad "the Windows release is not the expected set of files"
    printf '%s\n' "$WWANTED" > "$TMP/wwanted"
    printf '%s\n' "$WACTUAL" > "$TMP/wactual"
    diff -u "$TMP/wwanted" "$TMP/wactual" | sed 's/^/    /' || true
fi

# No ELF name survives the crossing. A package that still says `openmmo` at the
# top, or `bin/pokeplatinum` with no suffix, is a Linux tree with .exe files in
# it and would run nothing.
if [ ! -e "$WSTAGE/openmmo" ] && [ ! -e "$WSTAGE/bin/pokeplatinum" ]; then
    ok "no unsuffixed program and no shell front door came along"
else
    bad "the Windows release carries a Linux program name"
fi

# The front door, which on this host is a batch file and is parsed a line at a
# time by a program that has wanted CRLF since DOS.
if [ -f "$WSTAGE/OpenMMO.cmd" ] &&
   grep -q 'bin\\openmmo-launch\.exe' "$WSTAGE/OpenMMO.cmd"; then
    ok "the front door is a .cmd that starts the launcher out of bin"
else
    bad "OpenMMO.cmd does not start the launcher"
fi
if [ "$(tr -dc '\r' < "$WSTAGE/OpenMMO.cmd" | wc -c)" -eq \
     "$(wc -l < "$WSTAGE/OpenMMO.cmd")" ]; then
    ok "and every one of its lines ends CRLF"
else
    bad "OpenMMO.cmd is not CRLF throughout"
fi

# The Windows folder is the Linux one with the names changed and nothing added:
# the same rule, so the same by-name check, plus the .exe spelling of the one
# program that was taken out.
wbyname=1
for f in LICENSE LICENSE.GPL-3.0 README.md MANIFEST CREDITS \
         res/launcher/CREDITS mods/PUT-PACKAGES-HERE.txt \
         rom/PUT-THE-ROM-HERE.txt bin/openmmo-client.exe; do
    [ -e "$WSTAGE/$f" ] && { bad "$f is back in the Windows package"; wbyname=0; }
done
[ "$wbyname" -eq 1 ] && ok "nothing but the game and what it opens travels here either"

# The sha256 file says which host it is for. Two archives of the same version
# differ only inside, and this is the line that tells them apart.
grep -q '^host windows-x86$' "$WOUT/openmmo-t-windows-x86.sha256" \
    && ok "the hashes name the host they were taken for" \
    || bad "the sha256 file does not say host windows-x86"

# And the same no-game-data claim, because this is the artifact that travels.
for d in rom mods logs; do
    if [ ! -d "$WSTAGE/$d" ]; then
        bad "$d/ is not in the Windows release"
    elif [ -z "$(ls -A "$WSTAGE/$d")" ]; then
        ok "$d/ is in the Windows release and ships empty"
    else
        bad "$d/ holds something: $(ls -A "$WSTAGE/$d" | tr '\n' ' ')"
    fi
done
grep -q 'PC_MODS_DIR=%~dp0mods' "$WSTAGE/OpenMMO.cmd" \
    && ok "the front door points the game at that mods/ with an absolute path" \
    || bad "OpenMMO.cmd does not set PC_MODS_DIR from %~dp0, a relative one loads nothing"

# The zip is the directory, not a rearrangement of it, the same claim the
# tarball is held to, on the archive Explorer opens by double-clicking.
if command -v zip >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1; then
    "$PKG" --host windows --build "$WBUILD" --out "$WOUT" --version t >/dev/null 2>&1 \
        || bad "the zip was not written"
    mkdir -p "$TMP/wunpacked"
    if unzip -q "$WOUT/openmmo-t-windows-x86.zip" -d "$TMP/wunpacked" 2>/dev/null; then
        WUNPACKED=$(cd "$TMP/wunpacked/openmmo" && find . -type f -printf '%P\n' | sort)
        [ "$WUNPACKED" = "$WWANTED" ] && ok "the zip unpacks to that same release" \
                                      || bad "the zip is not what was staged"
        if [ -d "$TMP/wunpacked/openmmo/rom" ] &&
           [ -d "$TMP/wunpacked/openmmo/mods" ] &&
           [ -d "$TMP/wunpacked/openmmo/logs" ]; then
            ok "and the three empty directories survive it here too"
        else
            bad "the zip dropped rom/, mods/ or logs/ for being empty"
        fi
    else
        bad "the zip did not unpack"
    fi
else
    echo " SKIP zip/unzip not installed, the archive itself is unchecked"
fi

if [ "$fails" -gt 0 ]; then
    echo "package: $fails check(s) FAILED"
    exit 1
fi
echo "package: all checks passed"
