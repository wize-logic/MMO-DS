#!/bin/sh
# The 32-bit ARM build, as far as an x86 machine can go.
#
#   1. The ABI drifts. Four flags, and every one of them is a place where the
#      ARM defaults and i386's disagree, enum width, the sign of a bare
#      char, position independence, and the width of a time_t. They are the
#      guest struct layout, not tuning, and the failure they cause is a save
#      that nothing else can read.
#   2. The compiler is not the one this meant. brew puts a bare-metal
#      arm-none-eabi-gcc on $PATH and a plain host gcc answers to $(CC); the
#      first fails at the link in a way that reads like one of ours and the
#      second builds a perfectly good x86 binary out of a makefile called
#      .arm. Reading the compiler's own predefined macros is the check that
#      cannot be fooled by either.
#   3. An x86 PATH is still reachable. This tree is full of legal x86,
#      platform.c walks an i386 frame chain by hand and the whole Windows half
#      is 32-bit x86, so grepping the source answers nothing here. What the
#      ARM compiler was actually handed is the question, so this preprocesses
#      the client's translation units with the ARM flags and greps that.
#   4. It stops being what it claims. A link that quietly goes dynamic or
#      position-independent is a binary that will not start where it is going.
#      `armabi` reads that back out of the finished ELF.
set -eu

ROOT=${1:?usage: arm_test.sh <mmo-root> [engine-dir]}
ENGINE=${2:-}

MKARM="$ROOT/Makefile.arm"
MK="$ROOT/Makefile"

if [ ! -f "$MKARM" ]; then
    echo "arm: SKIP (no $MKARM)"
    exit 0
fi

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the client cross-builds for 32-bit ARM, and says so about itself:"

# ---------------------------------------------------------------- the ABI
# Five, not four. -D_FILE_OFFSET_BITS=64 is the one the time flag breaks: this
# compiler predefines both, asking for 32-bit time drops the large-file
# interface with it, and readdir() then returns EOVERFLOW on the first entry,
# every directory on the machine reads as empty, on the target only.
ABIFLAGS="-fno-short-enums -fsigned-char -fno-pie -D_TIME_BITS=32 -D_FILE_OFFSET_BITS=64"
for flag in $ABIFLAGS; do
    if grep -q -- "$flag" "$MKARM"; then
        ok "the ARM ABI carries $flag"
    else
        bad "$MKARM does not set $flag, guest struct offsets would diverge"
    fi
done

# The engine lays the same structs out and this client links its archive, so
# the two ABI lines are one line in two files. Read the engine's rather than
# trust that somebody updated both.
ENGMK="$ENGINE/pc/Makefile.arm"
if [ -n "$ENGINE" ] && [ -f "$ENGMK" ]; then
    miss=""
    for flag in $ABIFLAGS; do
        grep -q -- "$flag" "$ENGMK" || miss="$miss $flag"
    done
    if [ -z "$miss" ]; then
        ok "the engine's ARM build still uses the same five"
    else
        bad "the engine's ARM build no longer sets:$miss"
    fi
elif [ -n "$ENGINE" ] && [ ! -d "$ENGINE/pc" ]; then
    echo "  SKIP the engine's own ARM flags (no engine checkout at $ENGINE)"
elif [ -n "$ENGINE" ]; then
    bad "no $ENGMK, the engine checkout has no ARM build to agree with"
else
    echo "  SKIP the engine's own ARM flags (no engine directory given)"
fi

grep -q "include .*Makefile" "$MKARM" \
    && ok "Makefile.arm includes the ELF makefile rather than copying it" \
    || bad "Makefile.arm no longer includes $MK, three pipelines to fix three times"

# Every variable Makefile.arm overrides has to be overridable. A `:=` creeping
# into the base makefile makes the ARM build silently take the Linux value,
# which for ABI or CC is a build that cannot work.
for v in ABI CC AR LDFLAGS LDLIBS BUILD TARGET ABI_NAME; do
    if grep -qE "^$v *\??=" "$MK"; then
        ok "$MK leaves $v overridable"
    else
        bad "$MK sets $v with :=, Makefile.arm cannot change it"
    fi
done

# ------------------------------------------------------------ the compiler
ARMCC=$(make -s -f "$MKARM" -C "$ROOT" print-ARM_CC 2>/dev/null || true)
if [ -z "$ARMCC" ] || ! command -v "$ARMCC" >/dev/null 2>&1; then
    echo " SKIP the cross build (no ARM compiler, run"
    echo "       \$ENGINE/pc/install_arm_toolchain.sh, or apt install"
    echo "       gcc-arm-linux-gnueabihf qemu-user)"
    [ "$fail" -eq 0 ] && echo "arm: all checks passed" || echo "arm: FAILED"
    exit $fail
fi

# Resolved here rather than where it is first used: two checks below run an ARM
# binary, and the makefile is the only thing that knows which emulator and
# which sysroot this build was set up with.
QEMU=$(make -s -f "$MKARM" -C "$ROOT" print-QEMU_ARM 2>/dev/null || true)

# What the compiler says it is, which no makefile variable can fake. A host
# gcc handed to ARM_CC passes every grep above and fails right here.
defs=$("$ARMCC" -dM -E -x c /dev/null 2>/dev/null || true)
if printf '%s\n' "$defs" | grep -q '__arm__'; then
    ok "$ARMCC defines __arm__"
else
    bad "$ARMCC does not define __arm__, this is not an ARM compiler"
fi
x86def=$(printf '%s\n' "$defs" |
         grep -oE '__i386__|__x86_64__|__SSE2?__|__MMX__' | sort -u | tr '\n' ' ')
if [ -z "$x86def" ]; then
    ok "and defines nothing x86"
else
    bad "$ARMCC also defines: $x86def, that is an x86 compiler"
fi
# ARM has two float ABIs and they are not compatible. armhf is the one both
# devices' user space is built for.
if printf '%s\n' "$defs" | grep -q '__ARM_PCS_VFP'; then
    ok "and is the hard-float ABI (armhf), not soft"
else
    bad "$ARMCC is not the armhf ABI, __ARM_PCS_VFP is not defined"
fi

# --------------------------------------------------------------- the link
out=$(make -f "$MKARM" -C "$ROOT" status 2>&1 || true)
if printf '%s\n' "$out" | grep -q '^link: ok'; then
    n=$(printf '%s\n' "$out" | sed -n 's/^C files compiled: *\([0-9]*\).*/\1/p')
    ok "the client cross-links for ARM ($n files)"
else
    bad "the ARM link failed:"
    printf '%s\n' "$out" | grep -E 'error:|undefined reference' | head -5 | sed 's/^/       /'
fi

# What the finished ELF says about itself: 32-bit, ARM, not position
# independent, no interpreter, four-byte enums, hard float.
if out=$(make -s -f "$MKARM" -C "$ROOT" armabi 2>&1); then
    ok "${out#armabi: ok, }" 
else
    bad "the finished binary is not what it claims:"
    printf '%s\n' "$out" | sed 's/^/       /'
fi

# And the drift guard, which reads the engine's own flags rather than this
# file's list.
if out=$(make -s -f "$MKARM" -C "$ROOT" abicheck 2>&1); then
    case "$out" in
        *"(linux-armhf)"*) ok "abicheck names this ABI and passes" ;;
        *) bad "abicheck does not name linux-armhf: $out" ;;
    esac
else
    bad "abicheck failed:"
    printf '%s\n' "$out" | sed 's/^/       /'
fi

# --------------------------------------------------------- no x86 survives
cf=$(make -s -f "$MKARM" -C "$ROOT" cflags 2>/dev/null |
     grep -v -e '^-MMD$' -e '^-MP$' || true)
if [ -z "$cf" ]; then
    bad "Makefile.arm cannot say what it compiles with, no cflags target"
else
    hits=""
    for f in "$ROOT"/src/*.c; do
        if "$ARMCC" $cf -E "$f" 2>/dev/null | grep -v '^#' |
             grep -qE 'xmmintrin|emmintrin|immintrin|__builtin_ia32_|__i386|__x86_64'; then
            hits="$hits $(basename "$f")"
        fi
    done
    if [ -z "$hits" ]; then
        ok "no x86 header, builtin or macro survives into the ARM build"
    else
        bad "an x86 path is still reachable in:$hits"
    fi
fi

# ------------------------------------------------- the two halves agree on
# ARM
ECF=$(make -s -f "$ENGMK" cflags 2>/dev/null | tr '\n' ' ' || true)
if [ -z "$ECF" ]; then
    echo "  SKIP the client/mod layout seam (the engine cannot say what it compiles with)"
else
    probe="${TMPDIR:-/tmp}/arm_seam_$$.c"
    cat > "$probe" <<'PROBE'
#include "widget.h"
#include <stdio.h>
#include <stddef.h>
int main(void)
{
    printf("align u64=%zu s64=%zu\n", _Alignof(u64), _Alignof(s64));
    printf("mmo_widget_row=%zu value@%zu\n",
           sizeof(mmo_widget_row), offsetof(mmo_widget_row, value));
    printf("mmo_widget=%zu mmo_screen=%zu part@%zu\n",
           sizeof(mmo_widget), sizeof(mmo_screen), offsetof(mmo_screen, part));
    return 0;
}
PROBE
    a="${TMPDIR:-/tmp}/arm_seam_lib_$$"
    b="${TMPDIR:-/tmp}/arm_seam_mod_$$"
    # The mod compile takes the ENGINE's flags, prelude included, which is
    # what puts the engine's own typedefs in scope ahead of ours, plus our
    # include path, which is exactly how a mod source reaches these headers.
    if "$ARMCC" $cf -I"$ROOT/include" -static -o "$a" "$probe" 2>/dev/null &&
       "$ARMCC" $ECF -I"$ROOT/include" -static -o "$b" "$probe" 2>/dev/null &&
       [ -n "$QEMU" ] && command -v "$QEMU" >/dev/null 2>&1; then
        la=$("$QEMU" "$a" 2>/dev/null)
        lb=$("$QEMU" "$b" 2>/dev/null)
        if [ "$la" = "$lb" ] && [ -n "$la" ]; then
            ok "the client's compile and the mod's lay the shared structs out alike"
        else
            bad "the two halves disagree about the shared structs:"
            printf 'library: %s\n' "$la" | sed 's/^/       /'
            printf 'mod:     %s\n' "$lb" | sed 's/^/       /'
        fi
        case "$la" in
            *"align u64=4 s64=4"*) ok "and a 64-bit integer is four-aligned, as i386 has it" ;;
            *) bad "a 64-bit integer is not four-aligned: $la" ;;
        esac
    else
        echo "  SKIP the client/mod layout seam (a probe would not build or there is no qemu)"
    fi
    rm -f "$probe" "$a" "$b"
fi

# The other half of the same invariant: aligning the typedefs is only the whole
# answer while every 64-bit member goes through them. A raw long long, an
# int64_t or a double declared in a shared header keeps its natural 8-byte
# alignment on ARM whatever the typedefs say, and puts the seam back.
raw=$(grep -rlnE "^[[:space:]]+(unsigned )?(long long|double|int64_t|uint64_t)[[:space:]]+[a-z_]+" \
        "$ROOT"/include/*.h 2>/dev/null | xargs -r -n1 basename | tr '\n' ' ')
if [ -z "$raw" ]; then
    ok "and no shared header declares a 64-bit member that bypasses them"
else
    bad "these declare a raw 64-bit member, which the typedefs cannot align: $raw"
fi

# ----------------------------------------------------------------- and it runs
if [ -z "$QEMU" ] || ! command -v "$QEMU" >/dev/null 2>&1; then
    echo " SKIP running it (no $QEMU, apt install qemu-user)"
else
    bin=$(make -s -f "$MKARM" -C "$ROOT" print-TARGET 2>/dev/null || true)
    if [ -n "$bin" ] && [ -x "$bin" ]; then
        v=$("$QEMU" "$bin" version 2>&1 | head -1 || true)
        case "$v" in
            *openmmo-client*) ok "and an ARM binary runs here under qemu: $v" ;;
            *) bad "the ARM binary did not run under qemu: $v" ;;
        esac
    else
        bad "no ARM binary at $bin to run"
    fi
fi

# ------------------------------------------------------- and the other ARM
MKAND="$ROOT/Makefile.android"
if [ ! -f "$MKAND" ]; then
    echo "  SKIP the Android build (no $MKAND)"
else
    ANDCC=$(make -s -f "$MKAND" -C "$ROOT" print-CC 2>/dev/null || true)
    if [ -z "$ANDCC" ] || ! command -v "$ANDCC" >/dev/null 2>&1; then
        echo " SKIP the Android build (no NDK, unpack one under"
        echo "       \$HOME/.local/opt/android-ndk-r27c, or set NDK)"
    else
        out=$(make -f "$MKAND" -C "$ROOT" status 2>&1 || true)
        if printf '%s\n' "$out" | grep -q '^link: ok'; then
            n=$(printf '%s\n' "$out" | sed -n 's/^C files compiled: *\([0-9]*\).*/\1/p')
            ok "the client cross-links for Android ($n files)"
        else
            bad "the Android link failed:"
            printf '%s\n' "$out" | grep -E 'error:|undefined' | head -5 | sed 's/^/       /'
        fi
        if out=$(make -s -f "$MKAND" -C "$ROOT" androidabi 2>&1); then
            ok "$(printf '%s\n' "$out" | sed -n 's/^androidabi: ok -- //p')"
        else
            bad "the Android object is not what it claims:"
            printf '%s\n' "$out" | sed 's/^/       /'
        fi
        # time_t four bytes, off_t eight, a 64-bit integer four-aligned and an
        # enum four, compiled rather than asserted, since there is no device
        # here to run anything on.
        if make -s -f "$MKAND" -C "$ROOT" bionic >/dev/null 2>&1; then
            ok "and bionic agrees: time_t 4, off_t 8, s64 4-aligned, enum 4"
        else
            bad "bionic does not lay out what Makefile.android says it does:"
            make -s -f "$MKAND" -C "$ROOT" bionic 2>&1 | grep -E "error|static_assert|negative" | head -4 | sed 's/^/       /'
        fi
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "arm: all checks passed"
else
    echo "arm: FAILED"
fi
exit $fail
