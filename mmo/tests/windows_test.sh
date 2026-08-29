#!/bin/sh
# The Windows build, as far as a Linux machine can go.
#
#   1. The ABI drifts. `-mno-ms-bitfields -mno-align-double` are the guest
#      struct layout, not tuning; without them every offset diverges silently
#      and the failure is a save nothing else can read. `abicheck` knows only
#      the ELF flags, so this reads the PE pair back out of the engine's own
#      Windows makefile the same way.
#   2. The seam leaks. The whole point of include/platform.h and
#      include/sockets.h is that they are the only places that decide which
#      host this is. A caller that grows its own #ifdef compiles fine on both
#      and puts the decision back in four places, which is where it started.
#   3. It stops linking. Nobody cross-builds by accident, so a POSIX call
#      added to shared code can sit for weeks. Where the toolchain is
#      installed this links the archive and openmmo-client.exe for real, 
#      neither needs a third-party library, so it needs no network.
set -eu

ROOT=${1:?usage: windows_test.sh <mmo-root> [engine-dir]}
ENGINE=${2:-}

DOC="$ROOT/WINDOWS.md"
MKWIN="$ROOT/Makefile.win"
MK="$ROOT/Makefile"
PLAT="$ROOT/src/platform.c"
PLATH="$ROOT/include/platform.h"
SOCK="$ROOT/src/sockets.c"
SOCKH="$ROOT/include/sockets.h"
DEPSH="$ROOT/tools/win_deps.sh"

for f in "$DOC" "$MKWIN" "$MK" "$PLAT" "$PLATH" "$SOCK" "$SOCKH" "$DEPSH"; do
    if [ ! -f "$f" ]; then
        echo "windows: SKIP (no $f)"
        exit 0
    fi
done

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the client cross-builds for Windows, and the seam stays one place:"

# ---------------------------------------------------------------- the ABI
for flag in -mno-ms-bitfields -mno-align-double; do
    if grep -q -- "$flag" "$MKWIN"; then
        ok "the PE ABI carries $flag"
    else
        bad "$MKWIN does not set $flag, guest struct offsets would diverge"
    fi
done

# The engine's own Windows flags are where that pair comes from. A bump that
# moves them there and not here is exactly the silent divergence above.
EWIN="$ENGINE/pc/Makefile.win"
if [ -n "$ENGINE" ] && [ -f "$EWIN" ]; then
    drift=""
    for flag in -mno-ms-bitfields -mno-align-double; do
        grep -q -- "$flag" "$EWIN" || drift="$drift $flag"
    done
    if [ -z "$drift" ]; then
        ok "the engine's Windows build still uses the same pair"
    else
        bad "the engine no longer sets:$drift, this build's ABI is now a guess"
    fi
    grep -q "i686-w64-mingw32" "$EWIN" \
        && ok "and the same toolchain triple" \
        || bad "the engine's Windows build is no longer i686-w64-mingw32"
else
    echo "  SKIP the engine's own Windows flags (no $EWIN)"
fi

# ------------------------------------------------------------- the makefile
grep -q "include .*Makefile" "$MKWIN" \
    && ok "Makefile.win includes the ELF makefile rather than copying it" \
    || bad "Makefile.win no longer includes $MK, two pipelines to fix twice"

# Every variable Makefile.win overrides has to be overridable. A `:=` that
# creeps back into the base makefile makes the Windows build silently take the
# Linux value, which for ABI or CC is a build that cannot work.
for v in ABI CC LDFLAGS LDLIBS EXE TARGET; do
    if grep -qE "^$v *\?=" "$MK"; then
        ok "$MK leaves $v overridable"
    else
        bad "$MK sets $v with :=, Makefile.win cannot change it"
    fi
done

# --------------------------------------------------------------- the seam
#
#   * The engine's Windows build UNDEFINES _WIN32 (pc_prelude.h, via
#     PC_HIDE_WIN32). So a header the game half includes, which is every
#     header in include/, that branches on _WIN32 does not fail to compile:
#     it compiles the POSIX half into the Windows game, silently and always.
#     Same for a mod source.
#   * sockets.h pulls winsock2.h, whose SOCKET collides with the DS wifi
#     tree's. Reaching the game half through any header is what would do it.

# 1. Nothing the engine compiles, and no header it includes, may decide.
leak=$(cd "$ROOT" && grep -rlE "^[[:space:]]*#.*_WIN32" --include=*.c --include=*.h \
         include mods 2>/dev/null | grep -v "^include/sockets.h$" || true)
if [ -z "$leak" ]; then
    ok "no header and no mod source decides which host it is on"
else
    bad "the game half can see a host decision in: $(echo $leak | tr '\n' ' ')"
fi

# platform.h is the header every game translation unit includes, so it is the
# one this matters most for. Named separately because the message is the
# reason, and a list of one file does not carry it.
grep -qE "^[[:space:]]*#.*_WIN32" "$PLATH" \
    && bad "$PLATH branches on _WIN32, the game half cannot see that macro" \
    || ok "platform.h declares the same surface on every host"

# 2. sockets.h, which may be included by our own .c files and by nothing else.
#    A .h anywhere, or anything under mods/, puts winsock in a game TU.
sockh=$(cd "$ROOT" && grep -rl '#include "sockets.h"' --include=*.h \
          include src launcher viewer mods 2>/dev/null |
        grep -v "^include/sockets.h$" || true)
sockm=$(cd "$ROOT" && grep -rl '#include "sockets.h"' --include=*.c \
          mods 2>/dev/null || true)
if [ -z "$sockh" ] && [ -z "$sockm" ]; then
    ok "sockets.h is reachable from no header and no mod source"
else
    bad "sockets.h reached $(echo $sockh $sockm | tr '\n' ' ') -- winsock in a game TU collides with the DS stack's SOCKET"
fi

# 3. The host branches our own programs do carry, named. Each one is a place
# platform.h could not reach, and the list is short on purpose:
#
#    platform.h could not reach, and the list is short on purpose:
#      src/platform.c        the seam itself
#      src/sockets.c         the other seam
#      src/presence.c        the rich-presence IPC is a named pipe on one host
#                            and a unix socket on the other, and there is no
#                            third caller for platform.h to serve
#      launcher/fetch.c      HTTP over the same two socket stacks
#      launcher/update.c     chmod +x, which Windows does not have at all
#      launcher/xp_stat_compat.c  a whole file that exists only on Windows
#      viewer/view_ui_gtl.c  localtime_r, which msvcrt spells differently
#    An exact set, not a floor: a file that stops branching should leave this
#    list, because a list that names files which no longer do is a list nobody
#    can read a rule off.
branch=$(cd "$ROOT" && { grep -rlE "^[[:space:]]*#.*_WIN32" --include=*.c \
           src launcher viewer 2>/dev/null || true
         grep -rl '#include "sockets.h"' --include=*.c \
           src launcher viewer 2>/dev/null || true; } | sort -u)
expected="launcher/fetch.c
launcher/update.c
launcher/xp_stat_compat.c
src/handoff.c
src/net.c
src/platform.c
src/presence.c
src/sockets.c
viewer/view_ui_gtl.c
viewer/view_ui_mail.c"
if [ "$branch" = "$expected" ]; then
    ok "the host branches outside the seam are the ten this file names"
else
    bad "the host-branching files moved:"
    printf '%s\n' "$expected" > "${TMPDIR:-/tmp}/win_seam_want.$$"
    printf '%s\n' "$branch"   > "${TMPDIR:-/tmp}/win_seam_have.$$"
    diff "${TMPDIR:-/tmp}/win_seam_want.$$" "${TMPDIR:-/tmp}/win_seam_have.$$" |
        sed 's/^/       /'
    rm -f "${TMPDIR:-/tmp}/win_seam_want.$$" "${TMPDIR:-/tmp}/win_seam_have.$$"
fi

# ---------------------------------------------------------------- the link
CC_WIN="${MINGW:-i686-w64-mingw32}-gcc"
if ! command -v "$CC_WIN" >/dev/null 2>&1; then
    echo " SKIP the cross link (no $CC_WIN, apt install gcc-mingw-w64-i686)"
else
    out=$(make -f "$MKWIN" -C "$ROOT" status 2>&1 || true)
    if printf '%s\n' "$out" | grep -q '^link: ok'; then
        n=$(printf '%s\n' "$out" | sed -n 's/^C files compiled: *\([0-9]*\).*/\1/p')
        ok "the netcode and openmmo-client.exe cross-link ($n files)"
    else
        bad "the cross link failed:"
        printf '%s\n' "$out" | grep -E 'error:|undefined reference' | head -5 | sed 's/^/       /'
    fi
    # The window and the front door need the three mingw libraries, which
    # win_deps.sh puts in build/win/deps. Present is a build; absent is a skip
    # with the command that fixes it, not a quiet pass.
    if [ -f "$ROOT/build/win/deps/.ok" ]; then
        if make -f "$MKWIN" -C "$ROOT" viewer launcher >/dev/null 2>&1; then
            ok "and so do the window and the front door"
        else
            bad "the window or the front door would not cross-link"
        fi
    else
        echo "  SKIP the window and the front door (run \`make -C mmo -f Makefile.win winlibs\`)"
    fi
fi

# ----------------------------------------------------------------- the page
grep -q "i686-w64-mingw32" "$DOC" \
    && ok "WINDOWS.md names the toolchain" \
    || bad "WINDOWS.md no longer names i686-w64-mingw32"
for lib in SDL2 raylib freetype; do
    grep -qi "$lib" "$DOC" || bad "WINDOWS.md does not say where $lib comes from"
done
grep -qi "SDL2.dll" "$DOC" \
    && ok "and says which library ships beside the window" \
    || bad "WINDOWS.md does not mention SDL2.dll, the one file that must travel"

# ------------------------------------------------------------- the release
# The zip carries SDL2.dll, and the tree's notice for it names a version. The
# notice does not travel in the folder any more, but it is still what this
# project says it ships: a bumped pin with a stale CREDITS describes a library
# nobody built with, cheap to catch here, invisible otherwise.
sdlver=$(sed -n 's/^SDL2_VER=\(.*\)$/\1/p' "$ROOT/tools/win_deps.sh" | head -1)
if [ -z "$sdlver" ]; then
    bad "tools/win_deps.sh no longer pins SDL2_VER"
elif grep -qF "SDL2 $sdlver" "$ROOT/res/launcher/CREDITS"; then
    ok "CREDITS names the SDL2 the build fetches ($sdlver)"
else
    bad "CREDITS does not name SDL2 $sdlver, the shipped DLL and the tree's notice disagree"
fi

# And the doc has to still describe a Windows release that exists.
grep -q 'Makefile.win package' "$DOC" \
    && ok "WINDOWS.md says how the zip is built" \
    || bad "WINDOWS.md does not name the target that builds the zip"

# ------------------------------------------------------- the guest regions
# src/platform.c reserves the console's addresses at the PE entry point, and
# it spells them out because it may not include an engine header, that rule
# is what lets <windows.h> live in that file.
ARMREC="${ENGINE:-$ROOT/../engine/pokeplatinum}/tools/armrec/armrec_rt.h"
PLATC="$ROOT/src/platform.c"
if [ ! -f "$ARMREC" ]; then
    echo "  SKIP the guest region table (no armrec_rt.h at $ARMREC)"
else
    rfails=0
    # base=size pairs as the engine defines them, for every region the entry
    # point holds. ARM7 WRAM is spelled ARM_ARM7_WRAM_* and named differently
    # on the two sides, so the address is what is compared, not the name.
    for pair in ITCM:ARM_ITCM MAIN:ARM_MAIN_RAM SHARED:ARM_SHARED \
                WRAM:ARM_WRAM ARM7:ARM_ARM7_WRAM IO:ARM_IO \
                PAL:ARM_PALETTE OAM:ARM_OAM PORT:ARM_PORT_WINDOW; do
        sym=${pair#*:}
        b=$(sed -n "s/^#define ${sym}_BASE *0x\([0-9A-Fa-f]*\)u.*/\1/p" "$ARMREC" | head -1)
        z=$(sed -n "s/^#define ${sym}_SIZE *0x\([0-9A-Fa-f]*\)u.*/\1/p" "$ARMREC" | head -1)
        if [ -z "$b" ] || [ -z "$z" ]; then
            bad "armrec_rt.h no longer defines ${sym}_BASE/_SIZE"
            rfails=$((rfails + 1))
            continue
        fi
        # platform.c writes them as eight upper-case digits with a UL suffix.
        if ! tr -s ' ' < "$PLATC" | grep -qF "$(printf '{ 0x%08XUL, 0x%08XUL,' "0x$b" "0x$z")"; then
            bad "platform.c does not hold ${sym} at 0x$b size 0x$z"
            rfails=$((rfails + 1))
        fi
    done
    # VRAM is spelled as a range, not a size, and it is claimed one step after
    # the table above, which is exactly how it came to be left out and to
    # fail next. Computed here so the two spellings cannot disagree.
    vb=$(sed -n 's/^#define ARM_VRAM_BASE *0x\([0-9A-Fa-f]*\)u.*/\1/p' "$ARMREC" | head -1)
    ve=$(sed -n 's/^#define ARM_VRAM_END *0x\([0-9A-Fa-f]*\)u.*/\1/p' "$ARMREC" | head -1)
    if [ -n "$vb" ] && [ -n "$ve" ]; then
        vs=$(printf '%X' $(( 0x$ve - 0x$vb )))
        if ! tr -s ' ' < "$PLATC" | grep -qF "$(printf '{ 0x%08XUL, 0x%08XUL,' "0x$vb" "0x$vs")"; then
            bad "platform.c does not hold VRAM at 0x$vb size 0x$vs"
            rfails=$((rfails + 1))
        fi
    else
        bad "armrec_rt.h no longer defines ARM_VRAM_BASE/_END"
        rfails=$((rfails + 1))
    fi

    # And the GBA slot, which is pc_main.c's rather than armrec's: CTRDG_Init
    # probes it at boot whether or not a cartridge is modelled, so it is as
    # fixed as any of them.
    AGB="${ENGINE:-$ROOT/../engine/pokeplatinum}/pc/src/pc_main.c"
    if [ -f "$AGB" ]; then
        ab=$(sed -n 's/.*want = (void \*)0x\([0-9A-Fa-f]*\)u.*/\1/p' "$AGB" | head -1)
        al=$(sed -n 's/.*len = 0x\([0-9A-Fa-f]*\)u.*/\1/p' "$AGB" | head -1)
        if [ -n "$ab" ] && [ -n "$al" ]; then
            if ! tr -s ' ' < "$PLATC" | grep -qF "$(printf '{ 0x%08XUL, 0x%08XUL,' "0x$ab" "0x$al")"; then
                bad "platform.c does not hold the GBA slot at 0x$ab size 0x$al"
                rfails=$((rfails + 1))
            fi
        else
            bad "pc_main.c no longer spells the GBA slot as a literal want/len"
            rfails=$((rfails + 1))
        fi
    fi

    [ "$rfails" -eq 0 ] && ok "the entry point holds every fixed address the process takes"
fi

if [ "$fail" -eq 0 ]; then
    echo "windows: all checks passed"
else
    echo "windows: FAILED"
fi
exit $fail
