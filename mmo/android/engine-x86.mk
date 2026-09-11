# The engine's Android build, retargeted at 32-bit x86.
#
#   make -f mmo/android/engine-x86.mk BUILD=... status
#
# The engine tree's pc/Makefile.android is written for armeabi-v7a and says so
# in three places that are not the triple: -march=armv7ve in ABI, -mfpu=neon
# in HW_OPT, and the four --wrap=__aeabi_* flags plus pc_div0_wrap.o that give
# a division by zero the cartridge's answer on ARM. None of those exist on
# i686. This file includes that makefile and changes exactly those three,
# from OpenMMO's side, because the engine checkout is not this project's to
# write. Folding an ANDROID_ABI switch into
# pc/Makefile.android itself is the engine tree's call; this is what it would
# contain.
#
# WHY x86 AT ALL: the Android emulator is qemu, and on an x86 host with KVM it
# runs an x86 system image at native speed and an ARM one not at all (no KVM
# for a foreign guest; the translator that runs ARM apps on x86 images is
# binary translation and measures nothing about the app). So every emulator
# configuration in android/emu.sh, screen, density, cores, memory, driver,
# runs THIS build of the same sources. The desktop client is 32-bit x86
# already (mmo/Makefile: -m32), so nothing in the port is new to the
# instruction set; only the Android half is.
#
# WHAT THE THREE CHANGES ARE:
#
#   * ABI drops -march=armv7ve. i686-linux-android already defaults to SSE3
#     (clang -dM says __SSE3__), which is the vector unit the ELF build's
#     -msse2 asks for and more, so HW_OPT is plain -O3 -msse2 as pc/Makefile
#     has it. `override` because the engine file assigns both with := and
#     BASEFLAGS is expanded from ABI as pc/Makefile is read, so a plain
#     assignment after the include would be too late.
#
#   * LDFLAGS drops the four AEABI wraps. Reassigned AFTER the include and
#     NOT with override: `nound` passes LDFLAGS='$(LDFLAGS) -Wl,--no-undefined'
#     on its recursive command line, and an override here would beat that
#     and turn the gate into a rule that always passes. A plain assignment
#     loses to the command line, which is the order nound needs.
#
#   * pc_div0_wrap.o is an ARM assembly file and the engine adds it to
#     EXTRA_OBJS with `override +=`, which nothing on this side can take back.
#     The rule that assembles it is replaced instead (make says "overriding
#     recipe for target" twice and means it): on x86 the object is empty,
#     because the answer to a/0 == a is pc_div0.c's SIGFPE fixup, the same one
#     the Linux -m32 build has used since the particle emitter first divided
#     by a one-frame lifetime. pc_div0.c's #if picks the x86 half by itself.

OPENMMO_ENGINE ?= $(abspath $(dir $(firstword $(MAKEFILE_LIST)))/../../engine/pokeplatinum)

override ANDROID_ARCH := i686-linux-android
override ABI := -fno-short-enums -fsigned-char -fPIC -D_FILE_OFFSET_BITS=64
override HW_OPT := -O3 -msse2

include $(OPENMMO_ENGINE)/pc/Makefile.android

LDFLAGS = $(ABI) $(SANITIZE) -shared -Wl,-soname,libpokeplatinum.so \
          -Wl,--version-script=$(VERSCRIPT)

$(BUILD)/pc_div0_wrap.o: $(WROOT)/pc/src/pc_div0_wrap.s
	@mkdir -p $(dir $@)
	@echo '/* x86: the division-by-zero answer is pc_div0.c, not a wrapper */' \
	  | $(CC) $(ABI) -x c -c -o $@ -
