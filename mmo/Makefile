# Makefile, the native OpenMMO client.
#
# A standalone C program (socket + codec + session) built 32-bit so its struct
# layout matches the base engine it will later link with. It does not pull in
# the engine's objects or headers yet; that link is an additive change later,
# not an ABI change, because the ABI flags below are the engine's.

ROOT   := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
ENGINE ?= $(ROOT)/../engine/pokeplatinum

# ------------------------------------------------------------- the two builds
#
# RELEASE=0 is the working build: -O1 with debug info, which compiles fast and
# gives every crash a line. It is not what a player should be handed. RELEASE=1
# compiles our own code at -O2 with no debug info, and mmo/dist.sh turns it on
# and strips the copies it stages, a release is the only thing this
# distinction is for. Most of what it buys is the download: the game alone is
# 53 MB of DWARF and 10 MB of program.
#
# NDEBUG is deliberately NOT defined. The engine ships -DPM_KEEP_ASSERTS in
# both builds, so a release that quietly dropped its own checks would disagree
# with the half of the program that keeps them; and one dimension of difference
# (the optimizer) is one thing to suspect when a release-only bug turns up.
# THE ENGINE IS NOT COMPILED HARDER, and that is a measurement, not an
# oversight. -O2 there (the engine's own `OPT` knob, and its `make o2`) was
# built and benched on 2026-08-23: 9000 frames of the recorded replay, three
# runs each, 583.9 / 578.2 / 587.6 fps at the engine's -O1 against
# 576.2 / 588.1 / 583.7 at -O2, one distribution, no difference. The reason
# is in pc/Makefile: pc/hw, the two hardware models, is already -O3 -msse2,
# and the bench puts 76% of a frame inside them (3D rasterize 33%, 2D compose
# 43%). What -O2 would reach is the 14% "elsewhere", which did not move.
# So the release keeps the configuration the engine's own suite is run against
# every day rather than paying a divergence for nothing. ENGINE_OPT is still
# wired through FUSEDVARS for anyone who wants to try it again,
# `make -C mmo fused RELEASE=1 ENGINE_OPT=-O2`, and porttest was clean at
# -O2 on this tree, so it is a measurement that could change, not a hazard.
#
# A SECOND BUILD DIRECTORY, because none of these flags is a prerequisite of
# anything: built into one tree, a release object would be reused by the next
# working build and the other way round, and neither would say so. The engine
# separates its own detectors the same way. It also keeps a dist run from
# leaving the tree you play from compiled with SERVER_SETTABLE=0.
RELEASE ?= 0
RELDIR   = $(if $(filter 1,$(RELEASE)),-release)
ifeq ($(RELEASE),1)
  OPTFLAGS   ?= -O2
  VIEWOPT    ?= -O2
else
  OPTFLAGS   ?= -O1 -g
  VIEWOPT    ?= -O2 -g
endif

BUILD  ?= $(ROOT)/build$(RELDIR)
# ?= throughout this block, so Makefile.win can set what genuinely differs on
# the other toolchain and inherit the rest. A Windows makefile written as a
# copy pays for every pipeline fix twice; the port learned that lesson in
# pc/Makefile.win and this file is the same arrangement one
# level up. EXE is what a program is called there.
EXE    ?=
TARGET ?= $(BUILD)/openmmo-client$(EXE)
TESTBIN?= $(BUILD)/mmo-tests$(EXE)
LIB    ?= $(BUILD)/libopenmmo.a

CC     ?= gcc
AR     ?= ar

# The base engine's 32-bit ABI, on this toolchain. `make abicheck` confirms
# these still match what the engine compiles with, in both directions, so
# the layout can never silently drift. Makefile.win and Makefile.arm each
# replace this line wholesale rather than extending it, because none of the
# three toolchains shares a single ABI flag with either of the others: -m32
# does not exist on ARM, mingw is 32-bit by construction, and what ARM needs
# is four flags that i386 has no opinion about.
ABI    ?= -m32 -fno-pie
STD    ?= -std=gnu99 -fms-extensions -fno-strict-aliasing -fwrapv -fcommon
# -MMD -MP emits a .d beside every object listing the headers it read, so editing
# a header rebuilds what included it. Without this an edited struct leaves stale
# objects linked against the new one, and the tests report field-by-field garbage.
# mods/openmmo/include is on this path for ONE header: blackcompose.h, the
# Gen 5 sprite composer shared by the plugin that draws a loop live and the
# fill that bakes one. It has to live where the ENGINE's mod build can see it,
# that build gets `<mod>/include` and does not get mmo/include, so the
# lib and the launcher reach across to it rather than the mod reaching here.
CFLAGS ?= $(ABI) $(OPTFLAGS) $(STD) -MMD -MP -Wall -Wextra -Wno-unused-parameter -I$(ROOT)/include -I$(ROOT)/mods/openmmo/include -I$(BUILD)/gen
LDFLAGS?= $(ABI) -no-pie
LDLIBS ?= -lm

# ------------------------------------------- the toolchain's own system headers
#
# ASKED AT PARSE TIME, BECAUSE THE ANSWER IS NOT A CLIENT BUG AND MUST NOT
# ARRIVE AS ONE. Every object here is built for a guest ABI the host compiler
# reaches by a second set of headers, and when those are missing gcc does not
# say so: it drops the directory from its search path and carries on, so the
# first translation unit that includes <errno.h> dies with
#
#     /usr/include/linux/errno.h:1:10: fatal error: asm/errno.h: No such file
#
# several hundred lines into a build log, naming a header nobody in this
# repository wrote. Two releases died there on 2026-09-10: `apt install
# gcc-arm-linux-gnueabihf` took gcc-multilib out as a conflict, and
# gcc-multilib's ONE file is the symlink /usr/include/asm that made -m32 find
# the kernel headers at all. It then sat latent for a day behind a warm build
# tree, the publish that followed recompiled nothing and went out fine, and
# the next one, which had sources to rebuild, did not. So the probe is one TU
# on the real CC and the real ABI, and it runs before make builds anything.
#
# $(ABI) is whatever the including makefile set, so this asks mingw and the ARM
# cross about their own headers rather than about i386's.
#
# -include on an empty TU rather than a here-string, because make strips a
# comment from the middle of a $(shell ...) like any other: a probe written as
# printf '#include <errno.h>' is silently truncated at the #, runs nothing and
# passes on every host.
HOSTPROBE_GOALS := $(if $(MAKECMDGOALS),\
                     $(filter-out clean status cflags where print-%,$(MAKECMDGOALS)),all)
ifneq ($(strip $(HOSTPROBE_GOALS)),)
HOSTPROBE := $(shell $(CC) $(ABI) -E -include errno.h -x c /dev/null -o /dev/null 2>&1 \
                     | grep -m1 'fatal error')
ifneq ($(strip $(HOSTPROBE)),)
$(info )
$(info ======================================================================)
$(info  hostcheck: $(CC) $(ABI) cannot compile <errno.h> on this machine, so)
$(info  nothing here can be built. This is the toolchain, not the tree:)
$(info    $(HOSTPROBE))
ifneq ($(filter -m32,$(ABI)),)
$(info  A -m32 build wants the 32-bit kernel headers, which Ubuntu ships as a)
$(info  separate architecture:)
$(info    sudo apt-get install -y linux-libc-dev:i386)
$(info  Do NOT reinstall gcc-multilib to fix it -- apt takes the arm cross)
$(info  compiler back out, and mmo/Makefile.arm needs that.)
else
$(info  That is this toolchain's own sysroot, not the tree: install or repair)
$(info  the cross compiler named above before building again.)
endif
$(info ======================================================================)
$(error hostcheck: the toolchain cannot compile a system header)
endif
endif

# ---------------------------------------------------------------- the server
#
# The one address every copy of this client dials. It is not a setting: there
# is no launcher field, no config key and no flag for it, because which world a
# client belongs to is a fact about the build and not a preference. src/endpoint.c
# carries it XORed (include/obfstr.h) so that it is not printable in the binary.
#
# SERVER_GAME_PORT overrides the port the login server advertises; 0 takes the
# advertisement instead. Login advertises 7777 and that is the game this client
# dials, which is why 7777 is the value here.
#
# SERVER_SETTABLE is what separates this tree from a release. 1 compiles in the
# OPENMMO_SERVER / OPENMMO_GAMEPORT overrides the suite drives the client with,
# and that a session against a local server needs. mmo/dist.sh builds releases
# with 0, and then those two names are not in the program at all.
#
# SERVER_ROOT_KEY is the other half of the same fact: whose signature this
# client will accept for a ServerHello. The server's keypair is generated per
# machine (keys/build.gradle.kts) and gitignored, so a client built here trusts
# the servers here and nothing else, and a build pointed at another server has
# to be given that server's key too, or every session of it ends at the last
# line of the handshake. A pem (public or private) or the uncompressed point as
# hex; tools/rootkey.sh turns either into the bytes. The default below is this
# tree's own server.game/src/main/resources/game.public.pem, transcribed once so
# that a checkout with no keys generated yet still builds a client the committed
# capture fixtures verify against. mmo/dist.sh pins the live server's key for a
# release, next to the live address.
SERVER_HOST       ?= 127.0.0.1
SERVER_LOGIN_PORT ?= 2106
SERVER_GAME_PORT  ?= 7777
SERVER_SETTABLE   ?= 1
SERVER_ROOT_KEY   ?= 04f2f15c9a2d16be5bfead5329af7a9d1841e68431d1e680261b31cdfc7461dffce4b255957915a51ed70147583ecdc7c243f39c2f7d5f172a69073112e9d4b56b

# DEV_FEATURES is the other seam. A release is a Sinnoh client: the screens
# that belong to the ported regions, travel between PCs, HeartGold's
# Pokegear, the Johto and Kanto card, are in the working build for the
# people porting them and in no release. 1 here, 0 under RELEASE=1 (which is
# what mmo/dist.sh builds), and then the window has no button for them and
# the guest refuses their command; nothing at run time turns them back on.
# Written into the pin header with the rest so the objects that read it are
# rebuilt when it moves.
DEV_FEATURES      ?= $(if $(filter 1,$(RELEASE)),0,1)

# Written rather than passed as -D so that changing one of the five rebuilds
# exactly the objects that carry it: a -D that make cannot see is a stale
# endpoint.o pointing at the previous server.
PIN := $(BUILD)/gen/endpoint_pin.h

SRCS   := $(filter-out $(ROOT)/src/main.c,$(wildcard $(ROOT)/src/*.c))
LIBOBJS:= $(patsubst $(ROOT)/src/%.c,$(BUILD)/obj/%.o,$(SRCS))
OBJS   := $(LIBOBJS) $(BUILD)/obj/main.o
# tests/view_drive.c is not one of these: it is SDL and native, the other end of
# the window rather than a suite that links the client, and it is built by the
# VIEWDRIVE rule below.
# tests/blackanim_harness.c has a main of its own, a host tool over the
# compositor, built by hand, and linked in here it collides with tests/main.c.
# tests/hud_push.c is the same shape: a main of its own, built by the HUDPUSH
# rule below for offline_test.sh to press the in-game menu with.
TESTSRC:= $(filter-out $(ROOT)/tests/view_drive.c $(ROOT)/tests/blackanim_harness.c $(ROOT)/tests/hud_push.c,$(wildcard $(ROOT)/tests/*.c))
# viewer/view_geom.c is the window's geometry, and it is SDL-free on purpose so
# the -m32 suite can hold it to the properties a picture must not lose. It is
# compiled twice, here for the tests, and natively for the window itself.
# launcher/launch_plan.c is the same arrangement for the front door: settings,
# their file, and the two command lines Play turns them into, with the SDL in
# launcher.c beside it. launcher/launch_menu.c is the keys that menu takes,
# also SDL-free, so the suite can press them. launcher/feed.c and
# feed_verify.c are the other half of what it decides without a screen, the
# signed inventory an install is held to before a fork is spent, and are
# compiled here so the suite can point them at the feeds the official operator
# signed.
# viewer/view_status.c is the third: the window's end of the session-state
# page, SDL-free so the suite can hold what a player is told to the state that
# was published, with no window and no server. viewer/view_hud.c is the
# fourth: the host panel's page, layout and raster, the same way.
# viewer/view_ui.c is the fifth: the UI layer's placement, wrapping and the
# chat box's geometry, so a box the pointer must land in is arithmetic the
# suite holds rather than a picture somebody eyeballs. viewer/view_ui_game.c
# is the sixth and the largest: the official client's in-game UI, the HUD bar, its
# popup menus, the frame chrome, the party strip and every label on them,
# so the layout that says which of ten buttons a click hit is checked
# without a display.
TESTOBJ:= $(patsubst $(ROOT)/tests/%.c,$(BUILD)/obj/tests/%.o,$(TESTSRC)) \
          $(BUILD)/obj/viewer/view_geom.o $(BUILD)/obj/viewer/view_status.o \
          $(BUILD)/obj/viewer/view_hud.o $(BUILD)/obj/viewer/view_font.o \
          $(BUILD)/obj/viewer/view_ui.o \
          $(BUILD)/obj/viewer/view_ui_game.o \
          $(BUILD)/obj/viewer/view_ui_theme.o \
          $(BUILD)/obj/viewer/view_ui_skin.o \
          $(BUILD)/obj/launcher/launch_plan.o $(BUILD)/obj/launcher/launch_menu.o \
          $(BUILD)/obj/launcher/feed.o \
          $(BUILD)/obj/launcher/feed_verify.o \
          $(BUILD)/obj/android/mmo_device_rules.o
DEPS   := $(OBJS:.o=.d) $(TESTOBJ:.o=.d)

# WHAT ONE OF THIS FILE'S VARIABLES HOLDS, for a script that has to agree with
# this file rather than keep a second copy of it: `make -s print-LAUNCHSRC`.
# mmo/package.sh reads the window's and the front door's source lists this way,
# so a file added to a program here is a file its staleness check knows about
# and there is no list to remember to update twice, the two disagreeing is
# what stopped a release on 2026-09-10. Builds nothing.
print-%:
	@printf '%s\n' '$($*)'

.PHONY: all lib status cflags clean test abicheck fused enginecheck patchcheck viewcheck \
        world worldcheck where \
        viewer launcher package dist porttest portbench portasan portubsan portmelon \
        portroot portdiff portreach portscope portbattle enginewip pincheck \
        datacheck tablediff import soundtrack
.DEFAULT_GOAL := all
all: $(TARGET)

# The netcode built as a standalone archive with an explicit C API (client.h),
# so a program that already owns main(), the engine, can link it in. The CLI
# and the test binary are two front ends over this same archive.
lib: $(LIB)

-include $(DEPS)

# Regenerated every make and moved into place only when it differs, so a build
# that changed nothing does not rebuild the world.
.PHONY: FORCE
FORCE:

$(PIN): FORCE
	@mkdir -p $(dir $@)
	@key="$$($(ROOT)/tools/rootkey.sh '$(SERVER_ROOT_KEY)')" || exit 1; \
	printf '%s\n' \
	  '/* generated by mmo/Makefile, do not edit, do not commit */' \
	  '#ifndef OPENMMO_ENDPOINT_PIN_H' \
	  '#define OPENMMO_ENDPOINT_PIN_H' \
	  '#define OPENMMO_PIN_HOST       "$(SERVER_HOST)"' \
	  '#define OPENMMO_PIN_LOGIN_PORT $(SERVER_LOGIN_PORT)' \
	  '#define OPENMMO_PIN_GAME_PORT  $(SERVER_GAME_PORT)' \
	  '#define OPENMMO_PIN_SETTABLE   $(SERVER_SETTABLE)' \
	  '#define OPENMMO_PIN_DEV_FEATURES $(DEV_FEATURES)' \
	  "#define OPENMMO_PIN_ROOT_KEY   $$key" \
	  '#endif' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi

$(BUILD)/obj/devenv.o: $(PIN)
$(BUILD)/obj/endpoint.o: $(PIN)
$(BUILD)/obj/session.o: $(PIN)

$(BUILD)/obj/%.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# The suites that read committed capture fixtures locate them from the repo
# root, so a test binary built anywhere still finds them.
TESTFLAGS := -DMMO_REPO_ROOT='"$(ROOT)/.."' -I$(ROOT)/src

$(BUILD)/obj/tests/%.o: $(ROOT)/tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TESTFLAGS) -I$(ROOT)/viewer -I$(ROOT)/launcher \
	    -I$(ROOT)/android/src -c -o $@ $<

# android/src/mmo_device_rules.c is the handheld's per-device defaults as
# arithmetic, panel, cores and driver in, thread count and scales out,
# with no Android header in it, so the suite holds the table of known devices
# to it here. The probe that fills the facts in is Android-only and stays in
# Makefile.android.
$(BUILD)/obj/android/%.o: $(ROOT)/android/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/obj/viewer/%.o: $(ROOT)/viewer/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/viewer -c -o $@ $<

$(BUILD)/obj/launcher/%.o: $(ROOT)/launcher/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/launcher -c -o $@ $<

$(LIB): $(LIBOBJS)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $(LIBOBJS)

$(TARGET): $(BUILD)/obj/main.o $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(BUILD)/obj/main.o $(LIB) $(LDLIBS)

$(TESTBIN): $(TESTOBJ) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) -o $@ $(TESTOBJ) $(LIB) $(LDLIBS)

# What this build compiles with, on one line, the way the engine's own
# makefile answers the same question. `abicheck` reads the engine's; the ARM
# suite reads ours, because the only honest way to ask "did an x86 path
# survive into the ARM build" is to preprocess a translation unit with the
# flags the build genuinely uses rather than the ones a script assumed.
cflags:
	@printf '%s\n' $(CFLAGS)

# One variable, for a shell script that needs to resolve a path this file
# already worked out, which compiler, which emulator, which binary. A script
# that spells those out itself is a second copy of the makefile that drifts.
print-%:
	@echo '$($*)'

# Build and print a machine-readable verdict. Never exits non-zero on a link
# failure, it records the failure in a marker and reports it as text, so a
# caller can grep the result without the make invocation itself aborting.
status:
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/link-failed
	@$(MAKE) -s -f $(firstword $(MAKEFILE_LIST)) all 2>$(BUILD)/link.log || touch $(BUILD)/link-failed
	@echo "C files compiled: $(words $(OBJS))"
	@if [ -f $(TARGET) ] && [ ! -f $(BUILD)/link-failed ]; then \
	   echo "link: ok ($(TARGET))"; \
	 else \
	   echo "link: *** FAILED -- see $(BUILD)/link.log ***"; \
	   grep -m3 -E 'undefined reference|multiple definition|error:|Error' $(BUILD)/link.log | sed 's/^/  /' || true; \
	 fi

# Deterministic self-tests (codec round-trips, frame/crypto known answers).
# Two front ends run here: the in-process suite that links libopenmmo.a, and
# realbin_test.sh, which drives the actual product binary ($(TARGET)) as a
# subprocess, a test that links its own copy of nothing.
#
# viewer_test.sh is the fifth, and it needs the two SDL programs: it drives the
# real window over a synthetic frame page and the real launcher with no display.
# They are prerequisites only where SDL2 is installed, the client builds and
# tests without it, and putting the whole suite behind a package it does not
# need would cost more than the two checks are worth. The script says SKIP when
# they are missing, so a machine without SDL2 is told rather than quietly
# running fewer checks.
#
# crowd_test.sh is the sixth, heap_test.sh the seventh, name_test.sh the eighth,
# label_test.sh the ninth and ceiling_test.sh the tenth; they are the five that
# run the fused build, because the overworld's pool capacities, the traps on
# them, the field's heaps, the width of a trainer name and where a nameplate
# lands on screen are all engine-side and nothing that links libopenmmo.a can
# see any of it. All five SKIP without `make fused` and without a ROM. Where
# they run, the crowd costs three eight-second boots, the heap one longer one, 
# it walks a crowd across three maps, which is the only way to see memory that
# is given back at the wrong time, the name two short ones, the label four, two
# of which dump the rasterizer's own polygon list so the projection can be
# compared with it, and the ceiling three: one that reads back the size each
# raised pool and heap was actually built at, and two that shrink a field heap
# until the map load cannot be paid for and check the run stops loudly.
#
# charmap_test.sh is the eleventh and needs only the engine checkout, not a build
# of it: it re-derives the generated charcode table from the engine's own
# character map, the way abicheck re-reads the engine's own flags. pincheck_test.sh
# is the twelfth and the same shape: it mints an engine-surface manifest into a
# temp file and breaks it seven ways, so `pincheck: ok` has to have been earned.
# It checks the tool, not today's drift, the committed manifest is allowed to go
# stale and `make pincheck` is what says so.
#
# world_test.sh is the fourteenth and the same shape one level out: it surveys
# the platinum field data the engine draws and the pret map data the server
# generates from, asserts the structure a Sinnoh reader will depend on, every
# land-data chunk's header accounting for its bytes, the terrain plane where the
# engine's own constants put it, every warp resolving, every event inside its
# matrix, and then requires the design notes to publish what it measured. It reads the
# engine checkout and the decomp submodules and writes nothing.
#
# idmap_gen_test.sh is the fifteenth and is charmap's shape again, over the item
# tables: it re-derives the committed shared-item table from the engine's item
# list and the server's own item names, and names the ids the two worlds are
# known to disagree about. The table it replaced translated ids nobody had
# compared, and both halves round-tripped their own numbering happily.
#
# text_gen_test.sh is the sixteenth and is the same shape once more, over the
# text archive, but with the half the item check has no equivalent of. The
# server mints text ids from the decomp submodule and the client resolves them
# against the engine checkout, so there are two copies of one archive in play; as
# well as re-deriving the committed bank-length table, this walks every id the
# server's catalogue can emit and requires the client's table to resolve it.
#
# tablediff_test.sh is the eighteenth and is the same question one level in
# again: the two checkouts holding the same bytes says nothing about the two
# halves numbering the same things. It walks the id classes both hold, species,
# moves, items, maps, flags, in the engine's constant lists and in the codegen
# renders the server compiles, and fails on any disagreement mmo/TABLES has not
# written down, or any it has written down that is no longer true. Neither half
# can do this for itself: each round-trips its own numbering happily, which is
# how the item map spent four phases translating ids that were already the same
# number. It SKIPs when the server has not been built.
#
# datacheck_test.sh is the seventeenth and generalizes that last observation:
# text is not the only archive held twice. Every tree mmo/DATA_SOURCES declares,
# field data, moves, species, text, the generated enum dumps, must hold the
# same bytes in the engine checkout and in the decomp submodule, or the client
# renders one world while the server computes another with both sides' own checks
# green. Unlike pincheck this runs live here rather than in a standalone target:
# there is nothing about it that goes legitimately stale, so a divergence is
# always a defect. It also reads codegen/build.gradle.kts, so a generator that
# starts reading the DS decomp fails until the trees it reads are declared.
#
# contest_test.sh is the play path's neighbour and SKIPs on the same terms: it
# drives the link contest's wire with two headless sessions and no engine. The
# scene is the engine's and wants a window; the group is not, and the queue, the
# seat, the relay's sender stamp and the agreed result can each be wrong on
# their own. Both bugs it found on its first live run are why it is here: a
# lobby short of four waited for a group nobody would tell to start, and the
# last two results settled on two threads at once.
#
# modwarn_test.sh compiles every mod source with the two warning classes the
# port's own -w hides, and only those two. -w is right for the engine sources
# that flag is really aimed at and wrong for ours: a call written above its
# definition with no prototype in scope is an implicit int() here and a hard
# error on the NDK's clang, so the fused build, the whole of this suite and a
# live headless drive can all be green while the Android build refuses it. That
# is not hypothetical, it shipped on 2026-08-26 and had to be fixed after.
# Nothing is built; each file goes to /dev/null on the command line `make -n`
# says the fused build would have used, so the check cannot drift from it.
#
# surfaces_test.sh is the nineteenth and watches a page rather than the code:
# The design notes maps the official client's screens onto the opcodes each one speaks,
# and everything on it is frozen except the two lists of opcodes only one side
# of the wire implements. Those move the moment somebody writes a codec, and a
# closed gap the page still lists reads as work outstanding when it is done. It
# reads the four declared lists back out of the page and requires
# GameProtocol.kt to still disagree in exactly those places. Nothing outside
# this repository, so it never SKIPs.
#
# pvp_test.sh is the forty-eighth and watches the rules contract for
# the competitive group. Eight of that group's packets are bound under a name
# from the group they collided with, and the page says so row by row, which is
# the claim that rots: rename one, move one or drop one and the page still
# reads as true. It walks the page's own wire table and requires
# GameProtocol.kt to bind each class at that opcode in that direction, and
# requires the misnamed rows to still be marked. Nothing outside this
# repository, so it never SKIPs.
#
# display_gen_test.sh is the twenty-first and is idmap-gen's shape over the
# display tables. The in-process suite already compares the type chart with
# the official client's own copy and pins three overlay strings (Sturdy, Hail
# as Snowscape, Protean). What that cannot see is the engine moving, so this
# re-derives the committed type table and the ability/move/species fill,
# then breaks the type chart five ways and refuses a hollow engine
# checkout. The overlay half re-derives only when OPENMMO_STRINGS_XML
# points at the official client catalogue; the in-process suite pins three strings.
#
# osk_gen_test.sh is the same shape over the naming-screen grid: it
# re-derives the committed 6x13 x 5-page table from
# src/applications/naming_screen.c and the engine charmap, so a pin
# bump that moves a key fails here rather than as a drawn keyboard
# that is no longer the game's.
#
# sprite_gen_test.sh is the twenty-second and is that shape over the sprite
# archives: the fourteen pl_otherpoke bases are read out of the engine's own
# BuildPokemonSpriteTemplate, and nothing else in the tree states them, so a base
# that drifts by two draws one species with another's palette and every other
# check passes. It re-derives the committed table and then breaks the function
# six ways, including an expression shape the generator does not know, which it
# must refuse rather than approximate. It needs the built ROM as well as the
# sources: the archives' member counts are a measurement, not a constant.
#
# sprite_oracle_test.sh is the twenty-third and is the strong half of that pair,
# on the fused build. It has the engine answer for every drawable combination
# (OPENMMO_SPRITE_DUMP=1) and diffs those 6,732 rows against what the client
# computes with no engine present, so nothing in between is pinned. It also
# measures the two behaviours the client refuses because of: past its last
# species the engine's battle-sprite path indexes off the end of the archive, and
# its icon path substitutes the placeholder and says nothing.
# settings_test.sh is the twenty-fourth and watches which says which
# of the three programs that hold settings owns each one. Its static half reads
# the front door's config keys and the window's default key map back out of the
# code, so a setting added to either fails until the page knows; its measured
# half is the one setting that used to be owned twice. BUTTON MODE decided what
# a pad bit meant out of the save, underneath the window's map, and now reports
# what the host chose, so the same L press has to draw nothing under `normal`
# and exactly what A draws under `l-is-a`. Four boots of the fused build from
# one minted save; that half SKIPs without a build or a ROM.
#
# battle_seam_test.sh is the twenty-sixth and is the one check on the fused build
# that measures who supplies a screen rather than what it draws. Every message
# between the battle's computing half and its presenting half crosses one seam
# this build owns, and the check is that the seam is complete (every message the
# scene executed was handed to it there), that a recorded fight played back
# through it presents the same bytes, that a word changed in the recording is the
# word the scene executes, without which the previous claim would pass on a
# source that was read and discarded, and that a source which stops mid-fight
# stops the fight. Five boots from one minted save; SKIPs without a build or a
# ROM.
#
# boot_test.sh is the twenty-seventh and is the fused check on the boot a
# player meets. A session used to walk the port's own opening (Nintendo /
# Game Freak / Pokemon, then Continue / New Game) or skip it into a local
# new save. Neither is the game. Three 200-frame boots: OPENMMO_SERVER
# prints "session title" and is off the copyright card by frame 200, a
# plain boot is still the port's, and OPENMMO_BOOT_WORLD still skips to
# the field. SKIPs without a build or a ROM.
#
# battle_cmd_test.sh is the twenty-ninth and watches the page
# that is the 67-opcode presentation contract. It re-reads the 67 names
# out of the engine enum, the three s2c lists out of GameProtocol.kt, and
# the mapped command numbers out of battle_anim.h, so a renamed opcode or
# a new Battle* binding fails until the page moves. Needs the engine
# header for the name half; the protocol half never SKIPs.
#
# anim_test.sh is the thirtieth and watches the price of a move
# animation the engine never drew. It re-reads the overlay's custom-move
# count, the fallback define and the last engine move id out of this
# tree, and, when the checkout and its ROM are present, the anim.s
# count and the NARC member count, so a page that still says 91 after
# an overlay row lands is a failed check rather than a stale price.
# The engine half SKIPs without that tree; the rest never does.
#
# battle_lab_test.sh is the thirty-first and is the check that the fused
# build can enter a fight from a cold boot through the port's lab. The
# six kinds start (one minted save, six short parallel boots); an unknown
# kind and a short spec exit 2; --use-item fires. SKIPs the boots without
# a fused build or a ROM; the name half SKIPs only without the checkout.
# tools/battle_lab.sh is the driver; `make -C mmo portbattle` is the
# on-demand one-fight form.
#
# play_test.sh is the twenty-eighth and is the play path without a person.
# Two headless clients against a real game server: create, join, place,
# walk, see each other, chat. It SKIPs when the client binary is missing
# or when login/game are not listening (default 2106 / scratch 7778), so
# a machine without a game server is not a red suite. The design notes is the page
# a person follows; this is the check that the path it describes still
# works. Never pointed at the shared 7777, those characters sit in
# different regions and a pair there is a map mismatch, not a play path.
#
# offline_test.sh is the play path's other half: one character carried out to
# a save file and back, five times over. Each round boots a session, presses
# Continue Offline through the HUD page (openmmo-hudpush is that press with no
# window), plays the image with no server, character select, a walk read off
# the step lines, SAVE from the menu, and offers the report back through the
# import door, expecting the same species on the same slots. It SKIPs on the
# same terms as play_test.sh, plus a fused build and a ROM.
#
# playthrough_test.sh is the thirty-seventh and is the story without a
# person. One developer character against the same scratch game: rival,
# starter, first wild, each gym, each HM wall, the five rooms, the Hall
# of Fame. It SKIPs when the binary or either listener is absent, same
# as play_test.sh, and prints the furthest gate reached as the front.
#
# widgets_test.sh is the thirty-third and watches the list of
# engine constructors a server box already uses. It re-reads the seated
# FieldMessage / Menu_MakeYesNoChoice / Menu_New calls out of
# openmmo_dialog.c and, when the checkout is present, the same
# constructors out of the engine headers, so a home-grown box or a
# renamed primitive fails until the page moves. The engine half SKIPs
# without that tree; the rest never does.
#
# poketch_test.sh is the thirty-eighth and watches the second screen.
# It re-reads the template's window, colour and roster out of the mod
# and, when the checkout is present, the same four constants out of
# the engine headers, so a rectangle that is no longer POKETCH_SCREEN_*
# or a roster that is no longer 0-5 fails until the design notes moves.
# Also holds the end state that is not uniform: draw_labels untouched,
# compose still the upper strip, only the walking log, the keyboard and
# the NET: line gated to no-field. The fused half dumps frame 200 of
# two apps so the hook's call is observed, not only patched. The static
# half never SKIPs; the engine constants SKIP without a checkout, the
# dumped frame without a fused build or a ROM.
#
# font_test.sh is the forty-first and watches host-surface text. The ROM
# font's cell is 16px; nameplates and the chat strip already blit it. The
# leftover second font was a 5x7 overlay on the HUD. The static half
# re-reads the cell and the "not a second 5x7" decision out of
# The design notes and openmmo_font.c; the live half dumps frame 200 of a
# session title and looks for the ROM glyph's shadow in the NET strip.
# The page half never SKIPs; the boot SKIPs without a fused build or a ROM.
#
# camera_test.sh is the forty-second and watches the field camera. The
# cartridge table stays (DEFAULT 667 units at 8.09°, game aspect 4:3);
# land data still loads four 32x32 chunks; there is no field_camera.c
# patch. The live half lab-warps Twinleaf Town at --aspect 16:9 and
# requires the extra columns lit on engine A (bedroom at the same aspect
# lights none). The page half never SKIPs; the engine constants SKIP
# without a checkout, the boot without a fused build, a ROM or the lab
# settle script.
#
# pace_test.sh is the forty-third and watches frame pacing. The game
# ticks once per VBlank; the engine pacer is 1/60 s; guest audio is
# 59.8261 Hz; the window presents on vsync or a 16+17+17 ms clock and
# does not interpolate published frames. The page half never SKIPs; the
# engine clocks SKIP without a checkout, the paced boot without a fused
# build or a ROM, the dummy window without the viewer.
#
# sound_test.sh is the forty-fourth and watches the sequence mixer. The
# SPU is 16 channels; eight players share them; cries use 14 and 15.
# music and sfx 0-100 scale those players. The page half never SKIPs;
# the engine constants SKIP without a checkout, the audio-lab pin
# without a fused build or a ROM.
#
# crowd_perf_test.sh is the forty-fifth and watches crowd-on-screen
# cost. Twelve remotes on Twinleaf Town at native stay above 60 fps;
# --hd3d 2 is under 60 on an outdoor map and is not the default. The
# page half never SKIPs; the
# engine bench SKIP without a checkout, the live half without a fused
# build, a ROM or the lab settle script.
#
# screenshot_test.sh is the forty-sixth and pins composed frames so a
# presentation regression is a failing test. The static half re-reads
# the pin out of the design notes. The live half dumps frame 200 of a
# session title, hashes 16:9 attract / session / bedroom view pages,
# and presents the session page stacked at --scale 1 and --scale 2.
# The page half never SKIPs; the boots SKIP without a fused build or a
# ROM, the composed-window half without openmmo-view.
#
# wide_margin_test.sh is the fortieth and watches what a wide frame's extra
# columns can hold. A 16:9 publish is 340 columns; the centre 256 are the DS
# panel and the rest are the 3D rasterizer's. The static half re-reads the
# sprite-X bound, the 256-column scanline and the 3D-or-black decision out of
# The design notes and the engine sources, so a copied compositor or a 2D
# scanline that grew would fail until the page moves. The live half boots the
# fused build twice at --aspect 16:9 (title, then BOOT_WORLD) and classifies
# the view page: a 2D title cannot light the margins, a field opens the 3D
# gate, engine B's margins stay black. The page half never SKIPs; the engine
# constants SKIP without a checkout, the boots without a fused build or a ROM.
#
# mapformat_test.sh is the thirty-fourth and watches the page of
# measured distance between Platinum's field data and the other two generations',
# and the answer that came out of it, which is that there is no converter.
# The Gen 4 half came out of the two pinned decomp checkouts, which is the half
# a submodule bump moves: it re-reads the terrain-attribute offsets, the
# land-data header order, the matrix caps, both script command tables and both
# map header counts, so a decompiled field or a newly named script command fails
# until the page moves. The Gen 5 half is re-derived out of a Black cartridge
# with the engine port's own reader, the archive count, the map container, and
# the three candidate collision bits that say walkability is a guess there. Last
# it holds the answer to its code: tools/modport.sh refuses a `map` or `text`
# recipe line by name, and a line that asks for one writes nothing. The decomp
# half SKIPs without the checkouts, the Gen 5 half without OPENMMO_GEN5_ROM; the
# refusal checks never SKIP.
#
# overlay_test.sh is the thirty-fifth and is the check that a hub is loose files
# over the player's image rather than a rebuild, and that authored content is
# gated like code. It first holds every runtime directory under mods/ (not the
# C plugin) to a mod.toml and no C, then cooks every authored package (a
# content/ or records/ tree: bodies, hub, sprigatito; not the imports recipe)
# from a copy of their tracked sources, freezes every record id, watches the
# built image, its ninja log and the port binary not move, and then boots the
# fused build with the packages so the game's own map getters and NARC
# readers answer for the appended header. The bodies package is also claimed
# on its own: gfx 276 / mmodel 470 as an NSBTX, prop 590 as an untextured
# NSBMD, a bodies-only boot serves those members, and without it they are
# not claimed. Every authored map header answers; every authored species id
# grows pl_pokegra, PC_LAB_SPRITE names it, and sprite.c locates it against
# that count. With the packages off the same member is not claimed, and
# with them on a vanilla member still comes from the image. Also holds the
# two content doors apart: `openmmo` in
# PC_MODS is a boot error. Then it lab-warps onto that header and dumps a
# field frame whose top screen has the magenta person and the cyan prop,
# getters answering is not a picture, and runs that load through the
# port's pc_scrcov.py so a script that never reaches SetTrainerFlag fails
# here. The cook half SKIPs without the checkout or python3, the boot
# half without a fused build or a ROM. The directory contract is
# mods/README.md.
#
# import_test.sh is the thirty-sixth and is the check that art ported out of a
# second cartridge reaches the member our own species table draws from. The
# package tracks a recipe and no bytes, so the test fills a COPY of it from an
# image this machine has, compares every destination against `openmmo-client
# sprite`, and then boots the fused build so the game's own NARC readers answer
# the ported bytes, with the archive still 2964 members, since a sprite import
# replaces where the hub's map appends. A last section reads a Gen 5 archive
# through the same porter and holds the refusal honest: the count, the stride,
# and the two member kinds that say the Gen 4 arithmetic addresses nothing
# there. The static half never SKIPs; the fill needs OPENMMO_IMPORT_ROM, the
# boot half a fused build and a ROM, the Gen 5 half OPENMMO_GEN5_ROM.
# windows_test.sh is the forty-seventh and is the only thing in this suite that
# looks at the other toolchain. Four programs cross-build to PE from these same
# sources, and three things can make that quietly untrue: the ABI
# pair drifting away from the engine's own Windows flags, a caller growing its
# own #ifdef and putting the host decision back in four places, or a POSIX call
# landing in shared code that nobody cross-builds for weeks. It re-reads the
# ABI out of $(ENGINE)/pc/Makefile.win, holds the seam rule over every source,
# and links openmmo-client.exe for real where the toolchain is installed,
# which needs no third-party library and no network. It cannot RUN any of it:
# no Linux machine executes a PE binary, so past the link it is a person on
# Windows. SKIPs the link without mingw, and the two windows without
# `make -f Makefile.win winlibs`.
TRACE  := $(ROOT)/traces/login-admin.trace
test: $(TESTBIN) $(TARGET)
	@$(TESTBIN)
	@$(ROOT)/tests/realbin_test.sh $(TARGET) $(TRACE)
	@$(ROOT)/tests/ci_test.sh $(ROOT)/ci.sh $(ROOT)/../.github/workflows/client.yml \
	    $(ROOT)/../start-server.sh
	@$(ROOT)/tests/package_test.sh $(ROOT)
	@$(ROOT)/tests/viewer_test.sh $(ROOT) $(BUILD)
	@$(ROOT)/tests/crowd_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/heap_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/name_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/label_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/ceiling_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/charmap_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/pincheck_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/world_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/idmap_gen_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/text_gen_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/display_gen_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/osk_gen_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/sprite_gen_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/sprite_oracle_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/settings_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/update_test.sh $(ROOT) $(BUILD)
	@$(ROOT)/tests/battle_seam_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/boot_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/play_test.sh $(ROOT) $(BUILD)
	@$(ROOT)/tests/offline_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/contest_test.sh $(ROOT) $(BUILD)
	@$(ROOT)/tests/playthrough_test.sh $(ROOT) $(BUILD)
	@$(ROOT)/tests/datacheck_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/tablediff_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/surfaces_test.sh $(ROOT)
	@$(ROOT)/tests/modwarn_test.sh $(ROOT)
	@$(ROOT)/tests/pvp_test.sh $(ROOT)
	@$(ROOT)/tests/battle_cmd_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/anim_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/battle_lab_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/widgets_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/poketch_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/wide_margin_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/font_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/camera_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/pace_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/sound_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/soundtrack_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/viewpage_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/crowd_perf_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/screenshot_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/mapformat_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/overlay_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/billboard_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/follower_field_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/import_test.sh $(ROOT) $(BUILD) $(ENGINE)
	@$(ROOT)/tests/windows_test.sh $(ROOT) $(ENGINE)
	@$(ROOT)/tests/arm_test.sh $(ROOT) $(ENGINE)

# Frame-channel drift guard. include/view_channel.h is our copy of the layout of
# the shared page the game publishes frames into, held here so the window can be
# built without the engine checkout on the include path. viewer/channel_drift.c
# compiles both copies into one program and compares every constant and every
# field offset, so a page that moves in the engine tree is reported as drift
# instead of turning into a viewer that reads each field one slot late.
#
# Native, not $(ABI): the viewer is a 64-bit program because the host's SDL2 is
# one, that ABI split is the reason the two sides meet in a page and not a
# link. The page itself is all uint32_t and so has the same layout either way,
# which this check also happens to confirm.
#
# SKIPs cleanly when the engine is absent, like enginecheck: the mirror is what
# lets this repo build a window without that tree, so needing it here would
# defeat the point. tests/viewpage_test.sh is the suite half, it is in `test`,
# compiles each header alone so a size mismatch is a FAIL rather than a
# crash, and refuses a live /dev/shm page of the wrong size.
VIEWCHECK := $(BUILD)/viewcheck
.PHONY: viewcheck
viewcheck:
	@if [ ! -f $(ENGINE)/pc/include/pc_view.h ]; then \
	   echo "viewcheck: SKIP (no engine headers at $(ENGINE))"; \
	 else \
	   mkdir -p $(BUILD)/viewcheck-inc; \
	   cp $(ENGINE)/pc/include/pc_view.h $(BUILD)/viewcheck-inc/pc_view.h; \
	   if [ -f $(ROOT)/mods/openmmo/patches/pc/include/pc_view.h.patch ]; then \
	     patch --silent --forward --fuzz=0 $(BUILD)/viewcheck-inc/pc_view.h \
	         $(ROOT)/mods/openmmo/patches/pc/include/pc_view.h.patch || exit 1; \
	   fi; \
	   $(CC) -O1 -Wall -Wextra -I$(ROOT)/include -I$(BUILD)/viewcheck-inc \
	       -I$(ENGINE)/pc/include \
	       -o $(VIEWCHECK) $(ROOT)/viewer/channel_drift.c && $(VIEWCHECK); \
	 fi

# The window, built from sources under mmo/ and from nothing else. NO RULE HERE
# MAY REACH INTO $(ENGINE): the whole point of mirroring the page in
# include/view_channel.h is that the program a player launches is this repo's and
# builds without that checkout present. `make viewcheck` is what keeps the mirror
# honest, and it is a separate, skippable target for exactly that reason.
#
# Native, not $(ABI): the game is -m32 because its struct layouts are the
# console's, and the host's SDL2 is a 64-bit ELF a 32-bit process can neither
# link nor dlopen. That split is why the two meet in a shared page at all, so the
# window is a 64-bit program and the page is all uint32_t.
VIEWER  ?= $(BUILD)/openmmo-view$(EXE)
# src/platform.c is the fifth of these and the reason the window has no #ifdef
# in it: the shared page, the child processes and the two or three directories
# a program has to be told about are one file (include/platform.h).
# THE WINDOW'S OWN FACE, COMPILED IN. A font read out of a folder beside the
# program is a font that can go missing, and this one is half the difference
# between a finished window and a debug overlay, so it is part of the binary
# rather than something to install. Noto Sans Medium, SIL Open Font License
# 1.1 (res/fonts/OFL.txt), which is the family the official client draws these same
# surfaces with. The theme's own face still wins where a player pointed at an
# extracted client, because that one carries CJK and this one does not.
NOTOTTF := $(ROOT)/res/fonts/NotoSans-Medium.ttf
NOTOSRC := $(BUILD)/gen/view_font_noto.c

$(NOTOSRC): $(NOTOTTF) $(ROOT)/tools/bin2c.py
	@mkdir -p $(dir $@)
	@python3 $(ROOT)/tools/bin2c.py $(NOTOTTF) $@ view_ui_noto

VIEWSRC := $(ROOT)/viewer/viewer.c $(ROOT)/viewer/view_geom.c \
           $(ROOT)/viewer/view_font.c $(ROOT)/viewer/view_input.c \
           $(ROOT)/viewer/view_status.c $(ROOT)/viewer/view_hud.c \
           $(ROOT)/viewer/view_ui.c \
           $(ROOT)/viewer/view_ui_draw.c $(ROOT)/viewer/view_ui_chat.c \
           $(ROOT)/viewer/view_ui_game.c $(ROOT)/viewer/view_ui_bar.c \
           $(ROOT)/viewer/view_ui_wins.c $(ROOT)/viewer/view_ui_party.c \
           $(ROOT)/viewer/view_ui_gtl.c $(ROOT)/viewer/view_ui_mail.c \
           $(ROOT)/viewer/view_ui_theme.c \
           $(ROOT)/viewer/view_ui_skin.c \
           $(ROOT)/src/charcode.c \
           $(ROOT)/src/deflate.c \
           $(ROOT)/src/devenv.c \
           $(ROOT)/src/platform.c \
           $(NOTOSRC)
VIEWHDR := $(ROOT)/viewer/view_geom.h $(ROOT)/viewer/view_font.h \
           $(ROOT)/viewer/view_input.h $(ROOT)/viewer/view_status.h \
           $(ROOT)/viewer/view_hud.h \
           $(ROOT)/viewer/view_ui.h $(ROOT)/viewer/view_ui_draw.h \
           $(ROOT)/viewer/view_ui_chat.h $(ROOT)/viewer/view_ui_game.h \
           $(ROOT)/viewer/view_ui_bar.h $(ROOT)/viewer/view_ui_wins.h \
           $(ROOT)/viewer/view_ui_party.h $(ROOT)/viewer/view_ui_gtl.h \
           $(ROOT)/viewer/view_ui_mail.h \
           $(ROOT)/viewer/view_ui_theme.h \
           $(ROOT)/viewer/view_ui_skin.h \
           $(ROOT)/include/view_channel.h $(ROOT)/include/text_channel.h \
           $(ROOT)/include/status_channel.h $(ROOT)/include/hud_channel.h \
           $(ROOT)/include/vsync_channel.h \
           $(ROOT)/include/charcode.h $(ROOT)/include/platform.h \
           $(ROOT)/include/deflate.h
SDL2_CONFIG ?= sdl2-config
VIEWFLAGS ?= $(VIEWOPT) -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
             -I$(ROOT)/include -I$(ROOT)/viewer -I$(ROOT)/src
# Where the three libraries come from, named once so the PE build can say the
# same thing about a mingw prefix (tools/win_deps.sh) instead of pkg-config.
SDL_CFLAGS      ?= $(shell $(SDL2_CONFIG) --cflags 2>/dev/null)
SDL_LIBS        ?= $(shell $(SDL2_CONFIG) --libs 2>/dev/null)
FREETYPE_CFLAGS ?= $(shell pkg-config --cflags freetype2 2>/dev/null)
FREETYPE_LIBS   ?= $(shell pkg-config --libs freetype2 2>/dev/null)
FREETYPE_RPATH  ?= -Wl,-rpath=$(shell pkg-config --variable=libdir freetype2 2>/dev/null)
VIEWLINK        ?= $(SDL_LIBS) $(FREETYPE_LIBS) $(FREETYPE_RPATH) -lm

viewer: $(VIEWER)

# The pin header is a prerequisite because the bar reads DEV_FEATURES out of
# it (viewer/view_ui_game.h). Its -I is on this rule and not in VIEWFLAGS: the
# launcher shares those flags and finds its feed pin by include order, so a
# gen directory ahead of $(dir $(FEEDPIN)) would hand a pinned build the
# unpinned header (update_test.sh caught exactly that).
$(VIEWER): $(VIEWSRC) $(VIEWHDR) $(PIN)
	@if [ -z "$(SDL_LIBS)" ]; then \
	   echo "viewer: *** SDL2 not found, install its development package,"; \
	   echo "viewer:     or set SDL2_CONFIG=/path/to/sdl2-config ***"; \
	   exit 1; \
	 fi
	@if [ -z "$(FREETYPE_LIBS)" ]; then \
	   echo "viewer: *** freetype2 not found, the host panel needs it for"; \
	   echo "viewer:     the same DejaVu face the launcher uses ***"; \
	   exit 1; \
	 fi
	@mkdir -p $(BUILD)
	$(CC) $(VIEWFLAGS) $(FREETYPE_CFLAGS) $(SDL_CFLAGS) -I$(BUILD)/gen -o $@ $(VIEWSRC) \
	    $(VIEWLINK)
	@echo "viewer: ok ($(VIEWER))"

# The front door: the window a player meets before a frame exists. Native,
# raylib + raygui for the interactive window, under mmo/ like the rest of it.
# launch_plan.c, which holds everything it decides without a screen, is also
# compiled -m32 into `test`. --shot still draws the 5x7 glyphs, no display.
LAUNCHER  ?= $(BUILD)/openmmo-launch$(EXE)
LAUNCHSRC := $(ROOT)/launcher/launcher.c $(ROOT)/launcher/launch_gui.c \
             $(ROOT)/launcher/launch_plan.c \
             $(ROOT)/launcher/nitrorom.c \
             $(ROOT)/launcher/speciescompose.c \
             $(ROOT)/src/blackcompose.c \
             $(ROOT)/launcher/soundcompose.c \
             $(ROOT)/launcher/followcompose.c \
             $(ROOT)/launcher/lookcompose.c \
             $(ROOT)/launcher/launch_menu.c \
             $(ROOT)/launcher/xp_stat_compat.c \
             $(ROOT)/launcher/feed.c $(ROOT)/launcher/feed_verify.c \
             $(ROOT)/launcher/fetch.c $(ROOT)/launcher/update.c \
             $(ROOT)/launcher/tls.c \
             $(ROOT)/src/sockets.c \
             $(ROOT)/src/crypto.c $(ROOT)/src/devenv.c \
             $(ROOT)/src/platform.c \
             $(ROOT)/src/token.c \
             $(ROOT)/src/cartridge.c $(ROOT)/src/imports.c \
             $(ROOT)/src/charcode.c \
             $(ROOT)/src/offline_import.c $(ROOT)/src/offline_chain.c \
             $(ROOT)/src/save_bundle.c \
             $(ROOT)/src/codec.c \
             $(ROOT)/viewer/view_font.c
LAUNCHHDR := $(ROOT)/launcher/launch_plan.h $(ROOT)/launcher/launch_menu.h \
             $(ROOT)/include/save_bundle.h \
             $(ROOT)/launcher/nitrorom.h \
             $(ROOT)/launcher/speciescompose.h \
             $(ROOT)/mods/openmmo/include/blackcompose.h \
             $(ROOT)/src/species_port.gen.h \
             $(ROOT)/include/species_port.h \
             $(ROOT)/launcher/soundcompose.h $(ROOT)/src/soundtables.gen.h \
             $(ROOT)/launcher/followcompose.h \
             $(ROOT)/launcher/lookcompose.h $(ROOT)/include/appearance.h \
             $(ROOT)/src/follower_fill.gen.h \
             $(ROOT)/src/follower_index.gen.h \
             $(ROOT)/include/cartridge.h $(ROOT)/include/imports.h \
             $(ROOT)/include/charcode.h $(ROOT)/src/charcode_glyphs.gen.h \
             $(ROOT)/launcher/launch_gui.h \
             $(ROOT)/launcher/feed.h \
             $(ROOT)/launcher/fetch.h $(ROOT)/launcher/update.h \
             $(ROOT)/launcher/tls.h $(ROOT)/launcher/mmo_mbedtls_config.h \
             $(ROOT)/viewer/view_font.h \
             $(ROOT)/include/platform.h \
             $(ROOT)/include/status_channel.h \
             $(ROOT)/include/text_channel.h \
             $(ROOT)/third_party/raygui.h
# The compiled-in update channel, the endpoint pin's arrangement (PIN above):
# a release names its feed URL and the operator's public key at build time so
# a player configures nothing, the way it pins the server. Both empty, the
# default, and every working build, makes a front door that only checks a
# feed launcher.cfg configures and never fetches. FEED_KEY is a PUBLIC key, a
# PEM path or the bare base64 SPKI; FEED_URL without one is refused here,
# where the operator can act, rather than shipping a fetch nobody vouches
# for. launcher.cfg rows still override each, which is how a tester points a
# pinned build at a channel of their own.
FEED_URL ?=
FEED_KEY ?=
# ?= so a test can pin a throwaway launcher without touching this build's
# generated header; the compile line includes the pin's own directory.
FEEDPIN  ?= $(BUILD)/gen/feed_pin.h

$(FEEDPIN): FORCE
	@mkdir -p $(dir $@)
	@key='$(FEED_KEY)'; \
	 if [ -n "$$key" ] && [ -f "$$key" ]; then \
	     key=$$(grep -v '^-' "$$key" | tr -d '\n\r\t '); \
	 fi; \
	 if [ -n '$(FEED_URL)' ] && [ -z "$$key" ]; then \
	     echo "feed pin: *** FEED_URL without FEED_KEY is a fetch nobody vouches for"; \
	     exit 1; \
	 fi; \
	 printf '%s\n' \
	   '/* generated by mmo/Makefile, do not edit, do not commit */' \
	   '#ifndef OPENMMO_FEED_PIN_H' \
	   '#define OPENMMO_FEED_PIN_H' \
	   '#define OPENMMO_PIN_FEED_URL "$(FEED_URL)"' \
	   "#define OPENMMO_PIN_FEED_KEY \"$$key\"" \
	   '#endif' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi

BREW_PC   := $(shell brew --prefix 2>/dev/null)/lib/pkgconfig
RAYLIB_CFLAGS ?= $(shell PKG_CONFIG_PATH="$(BREW_PC):$(PKG_CONFIG_PATH)" pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS   ?= $(shell PKG_CONFIG_PATH="$(BREW_PC):$(PKG_CONFIG_PATH)" pkg-config --libs raylib 2>/dev/null)
RAYLIB_RPATH  ?= $(shell brew --prefix raylib 2>/dev/null)/lib
# The library the front door needs, and where it is found at run time.
#
# $$ORIGIN FIRST, BECAUSE A RELEASE IS NOT THIS MACHINE. raylib is a shared
# library here and its soname (libraylib.so.600) is not one a Linux machine
# already has, it is homebrew's build, at a homebrew path. A launcher whose
# only runpath was that path started on the machine that built it and on no
# other, which is exactly what a player reported: the front door could not
# load libraylib.so.600 and no window ever opened. So the packaged copy sits
# beside the binaries and is looked for there first; the build-machine path
# stays behind it so a working tree still runs without a copy of its own.
LAUNCHLINK    ?= $(RAYLIB_LIBS) -Wl,-rpath,'$$ORIGIN' \
                 -Wl,-rpath,$(RAYLIB_RPATH) -lm

# ------------------------------------------------------------------ mbedTLS
#
# The TLS the update fetch speaks, built from
# the submodule pinned at third_party/mbedtls under our own configuration,
# launcher/mmo_mbedtls_config.h, the library's own mbedtls_config.h is never
# read, into one archive. Every library file is compiled; the ones the
# configuration leaves out compile to nothing. The flags are the launcher's
# own (native here, because the launcher is), so the archive and tls.o agree
# on every struct; Makefile.android sets MBEDTLS_FLAGS for the device's ABI.
MBEDTLS        := $(ROOT)/third_party/mbedtls
MBEDTLS_LIB    ?= $(BUILD)/libmbedtls-openmmo.a
MBEDTLS_FLAGS  ?= $(VIEWOPT) -std=gnu99 -w
MBEDTLS_CONF   := $(ROOT)/launcher/mmo_mbedtls_config.h
MBEDTLS_CFLAGS := -I$(MBEDTLS)/include -I$(ROOT)/launcher \
                  -DMBEDTLS_CONFIG_FILE='"mmo_mbedtls_config.h"'
ifeq ($(wildcard $(MBEDTLS)/library/ssl_tls.c),)
# The submodule is not checked out. Fetch the pinned commit, shallow, once,
# and read this makefile again, because the object list below is a
# wildcard over files that did not exist when make read it the first time.
$(MBEDTLS_LIB): FORCE
	@echo "mbedtls: third_party/mbedtls is empty; fetching the pinned submodule"
	@git -C $(ROOT)/.. submodule update --init --depth 1 -- mmo/third_party/mbedtls || { \
	   echo "mbedtls: *** git submodule update --init, mmo/third_party/mbedtls failed"; \
	   echo "mbedtls: *** (a network is needed once; the pin is in .gitmodules)"; \
	   exit 1; }
	@$(MAKE) -f $(firstword $(MAKEFILE_LIST)) $@
else
MBEDTLS_SRC := $(wildcard $(MBEDTLS)/library/*.c)
MBEDTLS_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(BUILD)/obj/mbedtls/%.o,$(MBEDTLS_SRC))

$(BUILD)/obj/mbedtls/%.o: $(MBEDTLS)/library/%.c $(MBEDTLS_CONF)
	@mkdir -p $(dir $@)
	@$(CC) $(MBEDTLS_FLAGS) $(MBEDTLS_CFLAGS) -I$(MBEDTLS)/library -c -o $@ $<

$(MBEDTLS_LIB): $(MBEDTLS_OBJ)
	@rm -f $@
	@$(AR) rcs $@ $^
	@echo "mbedtls: ok ($@, $(words $(MBEDTLS_OBJ)) objects)"
endif

# The roots a channel's certificate must chain to, as one C string: Mozilla's
# store as curl publishes it (third_party/cacert.pem; tools/roots.sh refreshes
# it), the certificates alone, the bundle's labels and comments are dropped,
# so nothing outside a base64 block reaches a string literal.
ROOTSGEN := $(BUILD)/gen/tls_roots.gen.h

$(ROOTSGEN): $(ROOT)/third_party/cacert.pem
	@mkdir -p $(dir $@)
	@{ echo '/* generated by mmo/Makefile from third_party/cacert.pem, do not edit, do not commit */'; \
	   echo 'static const char mmo_tls_roots_pem[] ='; \
	   sed -n '/^-----BEGIN CERTIFICATE-----$$/,/^-----END CERTIFICATE-----$$/p' $< | \
	       sed 's/^/"/; s/$$/\\n"/'; \
	   echo ';'; } > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi
	@echo "roots: $$(grep -c 'BEGIN CERTIFICATE' $<) certificates compiled in ($@)"

launcher: $(LAUNCHER)

$(LAUNCHER): $(LAUNCHSRC) $(LAUNCHHDR) $(PIN) $(FEEDPIN) $(ROOTSGEN) $(MBEDTLS_LIB)
	@if [ -z "$(RAYLIB_LIBS)" ]; then \
	   echo "launcher: *** raylib not found, brew install raylib (or put it on PKG_CONFIG_PATH)"; \
	   exit 1; \
	 fi
	@mkdir -p $(BUILD)
	$(CC) $(VIEWFLAGS) -I$(ROOT)/launcher -I$(ROOT)/third_party \
	    -I$(ROOT)/src -I$(dir $(FEEDPIN)) -I$(BUILD)/gen \
	    -I$(ROOT)/mods/openmmo/include \
	    $(RAYLIB_CFLAGS) $(MBEDTLS_CFLAGS) -o $@ \
	    $(LAUNCHSRC) $(MBEDTLS_LIB) $(LAUNCHLINK)
	@echo "launcher: ok ($(LAUNCHER))"
	@# Beside the binary, for package.sh to carry. Under the SONAME the
	@# loader will ask for, brew's file is libraylib.so.6.0.0 and the
	@# name in the binary is libraylib.so.600, so copying the file by its
	@# own name ships a library nothing will ever look for. The soname is
	@# read out of the link that was just made, so a raylib version bump
	@# follows.
	@#
	@# THE BUILD MACHINE'S COPY IS THE SOURCE, NOT WHATEVER ldd RESOLVES.
	@# $$ORIGIN is first in the runpath above, deliberately, so a release
	@# finds the copy travelling beside it, which means that once this
	@# rule has run, ldd resolves the soname to the staged copy and this
	@# step would copy a file onto itself. cp says "are the same file" and
	@# exits non-zero, so every relink of the launcher after the first one
	@# failed the build. It also meant the staged copy shadowed the system
	@# one forever, so a raylib upgrade could never reach a package.
	@soname=`readelf -d $@ 2>/dev/null | \
	    sed -n 's/.*NEEDED.*\[\(libraylib[^]]*\)\].*/\1/p' | head -1`; \
	 if [ -n "$$soname" ]; then \
	   dst="$(BUILD)/$$soname"; \
	   src="$(RAYLIB_RPATH)/$$soname"; \
	   if [ ! -f "$$src" ]; then \
	     src=`ldd $@ 2>/dev/null | awk -v s="$$soname" '$$1 == s { print $$3 }'`; \
	   fi; \
	   if [ -z "$$src" ] || [ ! -f "$$src" ]; then \
	     echo "launcher: *** $$soname is not resolvable here; a package built"; \
	     echo "launcher: *** from this tree would not start on another machine"; \
	   elif [ "`readlink -f \"$$src\"`" = "`readlink -f \"$$dst\"`" ]; then \
	     echo "launcher: $$soname is already the copy beside it"; \
	   else \
	     cp -f "$$src" "$$dst" && \
	     echo "launcher: $$soname staged beside it for the package"; \
	   fi; \
	 fi

# The other end of the window, for viewer_test.sh: a synthetic publisher, a
# counter for the picture it produced, and the player, SDL's own key state, a
# virtual game controller and a warped pointer, through view_input.c unmodified.
# Native and SDL like the two programs it drives, and it links their sources
# rather than a copy, so it exercises what the window ships.
VIEWDRIVE    := $(BUILD)/openmmo-viewdrive
VIEWDRIVESRC := $(ROOT)/tests/view_drive.c $(ROOT)/viewer/view_input.c \
                $(ROOT)/viewer/view_geom.c $(ROOT)/src/platform.c \
                $(ROOT)/src/devenv.c

$(VIEWDRIVE): $(VIEWDRIVESRC) $(VIEWHDR) $(PIN)
	@mkdir -p $(BUILD)
	$(CC) $(VIEWFLAGS) $(SDL_CFLAGS) -I$(BUILD)/gen -o $@ $(VIEWDRIVESRC) \
	    $(SDL_LIBS) -lm

# One HUD command into a running fused client's page: the press behind
# offline_test.sh's Continue Offline. -m32 like the game, because the page is
# that binary's struct; no SDL, no client library.
HUDPUSH := $(BUILD)/openmmo-hudpush

$(HUDPUSH): $(ROOT)/tests/hud_push.c $(ROOT)/include/hud_channel.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

test: $(HUDPUSH)

# Where SDL2 is installed, `make test` builds these three and viewer_test.sh
# drives them; where it is not, that script says SKIP. Declared here rather than
# on the `test` rule above because these names are defined below it.
SDL2_OK := $(shell command -v $(SDL2_CONFIG) >/dev/null 2>&1 && echo yes)
test: $(if $(SDL2_OK),$(VIEWER) $(LAUNCHER) $(VIEWDRIVE))

# The fused engine+client binary: the ENGINE's own build, driven from here.
# MODS_DIR points its mod mechanism at mods/ in this repo, so the client
# compiles with the game's flags, headers and ABI without the engine tree
# being written to; EXTRA_OBJS puts the netcode archive on the same link.
# Everything the build produces, objects and the binary, lands in $(FUSED),
# under this repo. The window is no longer part of it: `viewer` above builds
# ours from sources under mmo/, so nothing in this file compiles a program out
# of $(ENGINE) any more.
FUSED ?= $(BUILD)/fused
# What the engine's makefile calls what it builds. An app loads a library.
FUSED_NAME ?= pokeplatinum$(EXE)
# Which of the engine's two makefiles this build drives. They are one pipeline:
# pc/Makefile.win is pc/Makefile retargeted at i686-w64-mingw32, and everything
# above is parameterized so this is the only name that has to change.
ENGINE_MAKEFILE ?= $(ENGINE)/pc/Makefile
# What makes a build of the engine this client rather than the base game, named
# once so a sanitizer tree is built from the same sources as the ship one and
# cannot quietly measure a different program.
#
# MODS= / MODS_DIR= is the compile-time C plugin. PC_MODS / PC_MODS_DIR is the
# other door: runtime packages the fused binary reads at boot. Forward the
# directory so a `make fused` (and the engine Makefile's status/cook when
# invoked with these vars) looks in this repo's mods/, not pc/mods. An unset
# PC_MODS still loads nothing, listing openmmo there is a boot error.
PC_MODS_DIR ?= $(ROOT)/mods
# ENGINE_OPT lives here and not on the `fused` rule so that every rule which
# builds into a fused tree carries it: `portbench` rebuilds before it measures,
# and a bench that quietly recompiled the release tree at the engine's default
# would report the working build's frame time under the release build's name.
# EXTRA_FUSED_OBJS is a toolchain's hook onto the same link: the Android
# build puts its NativeActivity entry point there, because a .so the
# framework loads needs one and the desktop link must not have it.
EXTRA_FUSED_OBJS ?=
# Deferred, not immediate: a toolchain file includes this one and then adds to
# EXTRA_FUSED_OBJS, which a := would have expanded away before it got there.
FUSEDVARS = MODS_DIR=$(ROOT)/mods MODS=openmmo \
             EXTRA_OBJS='$(LIB) $(EXTRA_FUSED_OBJS)' \
             PC_MODS_DIR=$(PC_MODS_DIR) $(if $(PC_MODS),PC_MODS=$(PC_MODS)) \
             $(if $(ENGINE_OPT),OPT=$(ENGINE_OPT))
# Not a prerequisite of the window: a machine with no SDL2 can still build and
# run the port headless (play.sh --dump-frames), and `make viewer` is what
# play.sh names when the window is missing.
fused: $(LIB)
	@$(MAKE) -s -f $(firstword $(MAKEFILE_LIST)) enginecheck
	@$(MAKE) -s -f $(firstword $(MAKEFILE_LIST)) patchcheck
	$(MAKE) -f $(ENGINE_MAKEFILE) -j$$(nproc) BUILD=$(FUSED) $(FUSEDVARS)
	@echo "fused: ok ($(FUSED)/$(FUSED_NAME))"

# Engine drift guard: the fused build stacks this client onto engine sources
# that advance in the engine repo next door, so it is authored against one known
# commit (ENGINE_COMMIT). Compare the engine's HEAD to that pin and warn loudly
# on drift, so a fused build that breaks after an engine bump reports "the engine
# moved" rather than a confusing compile error deep in engine sources. Never
# fatal: the engine legitimately advances under us and most moves don't break
# fused, this is context, not a gate. `fused` runs it first; it also stands
# alone. SKIPs cleanly when the engine has no reachable git HEAD.
ENGINE_COMMIT_FILE := $(ROOT)/ENGINE_COMMIT
#
# Drift alone turned out to be a poor question. The last bump crossed 43 engine
# commits and 6,080 changed files, and three of them touched anything this build
# depends on, so a banner that fires on the sha alone is a banner nobody can
# act on, and after a while nobody reads. `pincheck` (ENGINE_SURFACE) answers the
# question that can be acted on, and this target now leads with it: a drift whose
# surface is clean gets one line, and a drift that reaches something we patch or
# include gets the banner.
enginecheck:
	@pin=`grep -vE '^[[:space:]]*(#|$$)' $(ENGINE_COMMIT_FILE) | head -1 | tr -d '[:space:]'`; \
	 head=`git -C $(ENGINE) rev-parse HEAD 2>/dev/null`; \
	 if [ -z "$$head" ]; then \
	   echo "enginecheck: SKIP (no engine git HEAD at $(ENGINE))"; \
	   exit 0; \
	 elif [ -z "$$pin" ]; then \
	   echo "enginecheck: SKIP (no pinned commit in $(ENGINE_COMMIT_FILE))"; \
	   exit 0; \
	 fi; \
	 surface=`$(ROOT)/tools/engine_surface.sh --engine $(ENGINE) --root $(ROOT) --check 2>&1`; \
	 st=$$?; \
	 if [ "$$pin" = "$$head" ]; then \
	   echo "enginecheck: ok (engine at pinned `echo $$pin | cut -c1-10`)"; \
	 elif [ $$st -eq 0 ]; then \
	   echo "enginecheck: engine is `git -C $(ENGINE) rev-list --count $$pin..$$head 2>/dev/null` commit(s) past the pin"; \
	   echo "  and none of them reached this build's surface, context, not an action."; \
	 else \
	   echo "======================================================================"; \
	   echo " enginecheck: DRIFT, the engine moved under the fused build, and it"; \
	   echo " moved somewhere this build depends on."; \
	   echo "   pinned : $$pin"; \
	   echo "   engine : $$head"; \
	   echo " The fused build is known to work against the pinned commit. If it now"; \
	   echo " fails to build, this drift is the reason, not your client change."; \
	   echo " Bumping is a procedure, not an edit: mmo/ENGINE_COMMIT lists it, and"; \
	   echo " mmo/mods/openmmo/README.md, 'Pinning the engine, and knowing when it"; \
	   echo " moves', says when it is worth doing at all."; \
	   echo "======================================================================"; \
	 fi; \
	 echo "$$surface"; \
	 $(MAKE) -s -f $(firstword $(MAKEFILE_LIST)) enginewip

# The content half of the pin, standalone and with a real exit status: which of
# the engine paths this build depends on have moved since ENGINE_COMMIT was
# verified. Deliberately NOT in `make test`, it would turn every unrelated run
# red the moment a stranger's editor touched one of our headers, which is the
# wallpaper this was built to end. It gates a pin bump, and it reports inside
# enginecheck. tools/engine_surface.sh carries the reasoning.
pincheck:
	@$(ROOT)/tools/engine_surface.sh --engine $(ENGINE) --root $(ROOT) --check

# The same question one level out, and the opposite answer about where it runs:
# pokeplatinum is checked out twice, the engine the client renders from and the
# submodule the server generates from, and nothing compared them until this.
# It IS in `make test` (as datacheck_test.sh, which also breaks the tool seven
# ways), because unlike the engine surface there is nothing here that goes
# legitimately stale: two checkouts of one upstream either hold the same data or
# the two halves of the game have stopped describing the same world. This target
# is the same check standalone, for when the answer is what you came for.
datacheck:
	@$(ROOT)/tools/data_surface.sh --engine $(ENGINE) --root $(ROOT) --check

# The differential between the two halves' id tables, standalone. Same tool the
# test above runs; this is for when the answer is what you came for, it names
# the first disagreement in each class rather than only saying that there is one.
tablediff:
	@python3 $(ROOT)/tools/table_diff.py --engine $(ENGINE) --root $(ROOT)

# The other half of drift, and the one that cost this project an hour: the
# engine checkout can be MID-EDIT. Its own work runs in that tree beside ours,
# so a fused build taken while its sources are half-changed can be broken by
# something nobody committed and nobody will ever be able to reproduce. A
# rebuild an hour later fixes it and reads like a haunting.
#
# Not a gate, deliberately: a dirty engine builds fine almost always, and
# refusing to build would hand a stranger's editor a veto over this project.
# What it does is put the sentence in the log, so a fused build that suddenly
# fails to boot is read as "the engine is mid-edit" and not as "my patch".
enginewip:
	@n=`git -C $(ENGINE) status --porcelain -- . 2>/dev/null | wc -l`; \
	 if [ "$$n" -gt 0 ]; then \
	   echo " enginecheck: the engine checkout is MID-EDIT, $$n uncommitted file(s)."; \
	   echo "   A fused build taken now is built against sources nobody committed."; \
	   echo "   If it stops booting, re-check after that tree settles before"; \
	   echo "   believing anything here caused it:"; \
	   git -C $(ENGINE) status --porcelain -- . 2>/dev/null | head -8 | sed 's/^/     /'; \
	 fi

# Patch-context guard, and the reason it is fatal where enginecheck is not.
#
# The engine's compile rule applies mods/openmmo/patches/<path>.patch with
# `patch --forward` at the default fuzz. Measured against the engine's own
# history, that gives a patch two ways to go wrong and only one of them is
# loud. If a hunk stops applying outright the object is dropped, the link glob
# finds no .o, and the link fails on the file's symbols, loud, and you cannot
# miss it. If the context merely rots, fuzz still lands the hunk, somewhere,
# and the build succeeds with one SKIP-less line of nothing. This target is
# that second case: it re-cuts the same pipeline the compile uses (strip_asm,
# then the engine's own pc/patches diff, then ours) and insists on exact
# context. It found the encounter patch had been landing on fuzz since it was
# written, its blank context line had lost its leading space.
patchcheck:
	@$(ROOT)/tools/patch_survival.sh --engine $(ENGINE) --gate

# Drift guard: every ABI flag we pin must still be what the engine compiles
# with, in both directions, for whichever of the three toolchains this is.
#
# ABI_NAME is what this build calls itself, and it is in every line this target
# prints. Three makefiles build this client for three ABIs now, so a verdict
# that does not say which one it read is a verdict nobody can act on, and a log
# full of them cannot be told apart at all.
ABI_NAME ?= linux-x86-32

# The flags that are ABI and not tuning. The forward check below asks whether
# the engine still passes each of OURS; this list is what the reverse check
# needs, and the failure it catches is the opposite one, the engine gaining
# an ABI flag this client does not set.
#
# That is not hypothetical. -fsigned-char was added to the engine's ARM build
# after this client's ARM ABI was first written down, and every bare `char` in
# the tree changes sign with it; a check that only looked for our own flags in
# the engine's line would have called that agreement and let the two halves of
# one binary disagree about what a signed byte is.
ABI_SHAPED := -m32 -m64 -fpie -fPIE -fno-pie -fpic -fPIC -fno-pic \
              -fshort-enums -fno-short-enums -fsigned-char -funsigned-char \
              -fpack-struct -mno-ms-bitfields -mms-bitfields \
              -mno-align-double -malign-double
# The ones that carry a value, matched by prefix rather than exactly.
ABI_SHAPED_PFX := -fpack-struct= -D_TIME_BITS= -mfloat-abi= -mabi= -mfpmath=

# A SKIP HERE IS ONLY EVER "THERE IS NO ENGINE NEXT DOOR". Anything else,
# a checkout that has no makefile for this ABI, or one that cannot say what it
# compiles with because its toolchain is missing, is a failure and says so,
# because a check that skips forever is how a tree ends up building against
# nothing and being told every day that it passed.
abicheck:
	@ec=0; mk='$(ENGINE_MAKEFILE)'; \
	 if [ ! -d '$(ENGINE)/pc' ]; then \
	   echo "abicheck: SKIP ($(ABI_NAME)), no engine checkout at $(ENGINE);"; \
	   echo "abicheck:      these flags were compared with nothing: $(ABI)"; \
	   exit 0; \
	 fi; \
	 if [ ! -f "$$mk" ]; then \
	   echo "abicheck: *** FAILED ($(ABI_NAME)) -- there is no $$mk ***"; \
	   echo "abicheck:      the engine checkout is there and has no build for this ABI."; \
	   exit 1; \
	 fi; \
	 tmp=`mktemp`; \
	 eng="`$(MAKE) -s -f "$$mk" cflags 2>$$tmp | tr '\n' ' '`"; \
	 err="`cat $$tmp`"; rm -f $$tmp; \
	 if [ -z "$$eng" ]; then \
	   echo "abicheck: *** FAILED ($(ABI_NAME)) -- $$mk cannot say what it compiles with ***"; \
	   echo "$$err" | sed 's/^/abicheck:      /'; \
	   exit 1; \
	 fi; \
	 for f in $(ABI) $(STD); do \
	   case " $$eng " in *" $$f "*) ;; \
	     *) echo "abicheck: DRIFT ($(ABI_NAME)), engine no longer uses '$$f'"; ec=1;; esac; \
	 done; \
	 for f in $$eng; do \
	   keep=0; \
	   for p in $(ABI_SHAPED); do if [ "$$f" = "$$p" ]; then keep=1; fi; done; \
	   for p in $(ABI_SHAPED_PFX); do case "$$f" in "$$p"*) keep=1;; esac; done; \
	   if [ $$keep -eq 0 ]; then continue; fi; \
	   case " $(ABI) " in *" $$f "*) ;; \
	     *) echo "abicheck: DRIFT ($(ABI_NAME)), engine compiles with '$$f' and this client does not"; ec=1;; esac; \
	 done; \
	 if [ $$ec -eq 0 ]; then echo "abicheck: ok ($(ABI_NAME)) -- $(ABI)"; fi; \
	 exit $$ec

# ---------------------------------------------------------------- instruments
#
# The engine ships a harness this client can borrow whole: its own test suite, a
# per-frame benchmark, ASan and UBSan trees, a melonDS differential, a reach
# probe. Every one of them takes the binary it measures as an input, so every
# one of them can be pointed at the FUSED build instead of the base game's.
#
# None of them is the engine's target run as it stands, and that is the only
# reason these rules exist: `make -f pc/Makefile asan` builds into the engine
# checkout, and this project does not write there. Each rule below sets BUILD
# itself so every artifact lands under mmo/build.
#
# WHAT EACH IS WORTH OVER AN ENGINE THIS CLIENT CHANGED ON PURPOSE is
# The design notes, along with the ones that cannot be pointed here at all and
# what each cost when it was measured. Read it before trusting a green one: a
# build that moved the heaps cannot match a pinned memory image, and a
# differential whose oracle already disagrees with the base port is not
# measuring this client.
PYTHON  ?= python3
PC_ROM  ?= $(ENGINE)/build/rom/pokeplatinum.us.nds

# SKIP rather than fail where the build or the ROM is missing, the way the four
# fused checks in `make test` do: a machine with neither still runs the rest.
port_guard = if [ ! -x $(1)/pokeplatinum ]; then \
                 echo "$(2): SKIP (no build at $(1) -- run \`make -C mmo $(3)\`)"; exit 0; \
             fi; \
             if [ ! -f $(PC_ROM) ]; then \
                 echo "$(2): SKIP (no ROM at $(PC_ROM))"; exit 0; \
             fi

# The engine suite over our binary: every test in it that drives the binary,
# less the ones that shell `make` back into the engine tree and less `replay`,
# which a modded build cannot answer (the design notes says why, and the answer is
# not "it is broken"). Name tests to run fewer:
#     make -C mmo porttest PORT_TESTS=corpus
# `fast_forward` is not among them: this client no longer publishes the word
# that station drives, so it would be measuring the engine's pacer through a
# key nothing here presses.
PORT_TESTS ?= determinism mi_selftest png_decode widescreen hd3d view_channel \
              session save_durable save_lab battle_math clock_rollover corpus \
              polys cli state_digest sym selftest

# Run out of the view rather than out of the engine, and with no PC_BIN: under
# the view the suite's own default binary IS the fused build, and everything it
# mints or caches lands in the view's build/ instead of the engine's.
#
# The `cd` is not tidiness. The port takes its ROM at a path relative to the
# working directory, and pc_corpus.py strips every PC_ variable out of the
# environment it gives the port, so PC_ROM cannot reach it: the only way a
# station finds the ROM is that the directory it is run from has a build/rom in
# it. Run this from anywhere else and eight boots fail with "cannot open the ROM
# image", which reads like a missing ROM and is not one.
porttest: portroot
	@set -e; $(call port_guard,$(FUSED),porttest,fused); \
	 cd $(PORTROOT) && $(PYTHON) pc/tests/run_tests.py $(PORT_TESTS)

# Where a frame's time goes. BENCH_FRAMES and BENCH_SCRIPT are the engine
# rule's own knobs and are forwarded rather than copied, so this file never
# holds a second default for either.
portbench:
	@set -e; $(call port_guard,$(FUSED),portbench,fused); \
	 PC_ROM=$(PC_ROM) $(MAKE) -f $(ENGINE)/pc/Makefile bench -j$$(nproc) \
	     BUILD=$(FUSED) $(FUSEDVARS) \
	     $(if $(BENCH_FRAMES),BENCH_FRAMES=$(BENCH_FRAMES)) \
	     $(if $(BENCH_SCRIPT),BENCH_SCRIPT=$(BENCH_SCRIPT))

# The two detectors, each in its own tree beside the ship build. HALT=0 is not a
# shrug: two findings that belong to the engine sit on paths every run crosses,
# one on every boot that mints a save, one on every ball thrown in a battle,
# and halting on either ends the boot before the thing under test happens.
# tools/port_sanitize.sh catalogues the reports instead, carries the two by name
# with what proved each one the engine's, and fails on anything else.
ASANDIR  := $(BUILD)/asan
UBSANDIR := $(BUILD)/ubsan

portasan: $(LIB)
	$(MAKE) -f $(ENGINE)/pc/Makefile ASAN=1 ASAN_HALT=0 -j$$(nproc) \
	    BUILD=$(ASANDIR)/fused $(FUSEDVARS)
	@$(ROOT)/tools/port_sanitize.sh asan $(ROOT) $(ASANDIR) $(ENGINE) $(PC_ROM)

portubsan: $(LIB)
	$(MAKE) -f $(ENGINE)/pc/Makefile UBSAN=1 UBSAN_HALT=0 -j$$(nproc) \
	    BUILD=$(UBSANDIR)/fused $(FUSEDVARS)
	@$(ROOT)/tools/port_sanitize.sh ubsan $(ROOT) $(UBSANDIR) $(ENGINE) $(PC_ROM)

# The differential oracle: melonDS built headless into a program that writes the
# same trace the port writes. The mkdir is load-bearing, the engine's rule
# opens its cmake log inside $(BUILD) before anything has created it.
MELONHOST := $(BUILD)/melon/pcdiff-melon
portmelon:
	@mkdir -p $(BUILD)/melon
	$(MAKE) -f $(ENGINE)/pc/Makefile melon BUILD=$(BUILD)/melon

# An engine-shaped directory whose build/ is ours, and the thing that makes the
# rest of this block possible: four of the harness's programs find their root
# from their own path and their binary underneath it, and two of them write what
# they compute. tools/port_root.sh carries the reasoning; everything in the view
# is a symlink except the build/ it makes, which is the only thing any
# instrument writes to.
PORTROOT := $(BUILD)/portroot
portroot:
	@set -e; $(call port_guard,$(FUSED),portroot,fused); \
	 $(ROOT)/tools/port_root.sh $(ENGINE) $(PORTROOT) $(FUSED)/pokeplatinum $(MELONHOST)

# Run the port and the console over the same ROM and report where they first
# disagree. DIFF_ARGS is pcdiff.py's own flag set (--frames, --every, --input,
# --sav, --keep); nothing is defaulted here that the script already defaults.
portdiff: portroot
	@set -e; if [ ! -x $(MELONHOST) ]; then \
	     echo "portdiff: SKIP (no oracle -- run \`make -C mmo portmelon\`)"; exit 0; \
	 fi; \
	 $(PYTHON) $(PORTROOT)/pc/diff/pcdiff.py $(DIFF_ARGS)

# How far the fused build gets before it stops, and what stopped it. The script
# owns every default, including the frame cap, and this only forwards an
# override, a second copy of that number here is exactly how the engine's own
# `reach` went stale next door.
portreach: portroot
	@$(if $(REACH_FRAMES),REACH_FRAMES=$(REACH_FRAMES) )$(PORTROOT)/pc/reach.sh

# Which engine sources this client changes, the differential's blind spot, and
# the answer to "is this file ours now". Read out of the tree, so it cannot go
# stale the way a list in a document does.
portscope:
	@cd $(ROOT)/mods/openmmo/patches && \
	 n=`find . -name '*.patch' | wc -l`; \
	 echo "engine sources this client patches ($$n):"; \
	 find . -name '*.patch' | sed 's|^\./||; s|\.patch$$||; s|^|  |' | sort
	@cd $(ROOT)/mods/openmmo/src && \
	 n=`ls *.c | wc -l`; \
	 echo "new translation units compiled as game code ($$n):"; \
	 ls *.c | sed 's|^|  mods/openmmo/src/|'
	@echo "plus everything downstream of what those decide: heap sizes, the"
	@echo "overworld's pool capacities, who decides an encounter, name width."

# Start a fight from a cold boot of the fused build. The lab is the port's
# (PC_LAB_BATTLE / PC_LAB_USE_ITEM); this is the one way this tree drives
# it. BATTLE, BATTLE_AT, BATTLE_FRAMES and USE_ITEM are this rule's knobs
# and are not repeated as defaults anywhere else.
# A STATION IS RE-MINTED WHENEVER THE BINARY MOVES. The save is a save of THIS
# game, written by the port through the game's own setters, so a build that
# changes the save layout, or the new-game script, or the party the recipe
# mints, leaves the old one meaning something else. It does not fail: it
# BOOTS, and on 2026-09-01 a base.sav from 08-16 booted the Rowan intro under a
# battle station and the fight simply never started, which reads as the lab
# being broken. Cheap to remint, and never wrong.
BATTLE        ?= wild 25 5
BATTLE_AT     ?= 2600
BATTLE_FRAMES ?= 3000
USE_ITEM      ?=
portbattle:
	@set -e; $(call port_guard,$(FUSED),portbattle,fused); \
	 mkdir -p $(BUILD)/battle-lab; \
	 if [ ! -f $(BUILD)/battle-lab/base.sav ] \
	     || [ $(FUSED) -nt $(BUILD)/battle-lab/base.sav ]; then \
	     $(ROOT)/tools/battle_lab.sh mint $(ROOT) $(BUILD) $(ENGINE) \
	         $(BUILD)/battle-lab/base.sav; \
	 fi; \
	 cp $(BUILD)/battle-lab/base.sav $(BUILD)/battle-lab/run.sav; \
	 if [ -n "$(USE_ITEM)" ]; then \
	     $(ROOT)/tools/battle_lab.sh use $(ROOT) $(BUILD) $(ENGINE) \
	         $(BUILD)/battle-lab/run.sav "$(USE_ITEM)" $(BATTLE_AT) \
	         $(BATTLE_FRAMES); \
	 else \
	     $(ROOT)/tools/battle_lab.sh fight $(ROOT) $(BUILD) $(ENGINE) \
	         $(BUILD)/battle-lab/run.sav "$(BATTLE)" $(BATTLE_AT) \
	         $(BATTLE_FRAMES); \
	 fi

# Fill mods/imports from a cartridge this machine has. Content ported out of
# another Gen 4 image is served over the player's own image by the same overlay
# the hub uses, so it needs no rebuild and no cook, but its bytes are the
# cartridge's, so they are made here and never committed. IMPORT_ROM is the
# source image; OUT fills a copy instead of the tracked package. Authored
# packages cook with the engine makefile; see mods/README.md.
#
#   make -C mmo import IMPORT_ROM=~/pokeheartgold.us.nds
#   PC_MODS_DIR=$PWD/mmo/mods PC_MODS=imports mmo/build/fused/pokeplatinum
IMPORT_PKG ?= $(ROOT)/mods/imports
import:
	@if [ -z "$(IMPORT_ROM)" ]; then \
	   echo "import: IMPORT_ROM=<other-game.nds> is required" >&2; exit 2; \
	 fi
	@$(ROOT)/tools/modport.sh $(ENGINE) $(IMPORT_PKG) $(IMPORT_ROM) $(OUT)

# A PORTED WORLD, checked and then stood in. `mods/hgss` is 538 maps out of a
# Heart Gold cartridge, and loading one of them proves nothing about the other
# 537, so `worldcheck` boots a landmark of every kind headless and says what
# each drew, and `world` fills whatever needs filling, starts whatever is down
# and opens the window there. `tools/world.sh --where` is the list on its own.
# Neither is in `make test`: both need a filled package, and the payload is the
# player's cartridge's rather than this repository's.
#
#   make -C mmo worldcheck IMPORT_ROM=~/pokeheartgold.us.nds
#   make -C mmo world      IMPORT_ROM=~/pokeheartgold.us.nds USER=me PASS=...
worldcheck:
	@OPENMMO_IMPORT_ROM=$(IMPORT_ROM) $(ROOT)/tools/world.sh --check

world:
	@OPENMMO_IMPORT_ROM=$(IMPORT_ROM) $(ROOT)/tools/world.sh --play \
	    $(if $(USER),--user $(USER)) $(if $(PASS),--pass $(PASS))

where:
	@$(ROOT)/tools/world.sh --where

# The soundtrack packages: Sinnoh against another cartridge's whole sound.
# Two passes per game, both from the player's own image. The FONT pass
# (tools/soundfont.py, the rows in SOUNDFONTS) rewrites the instruments the
# Platinum-rendered music plays through; the TRACK pass (tools/soundtrack.py,
# the rows in SOUNDTRACKS) then replaces each mapped track with the other
# game's own rendition, carrying its bank and waves in beside the fonted
# archive. The font runs first on purpose: the track pass reuses waves that
# are already byte-identical in the archive it lands in, and after the font
# pass those are the donor game's. The launcher's `soundtrack` key picks a
# package at play time and refuses by name when it is not composed.
#
#   make -C mmo soundtrack SOUNDTRACK_HG=~/pokeheartgold.nds
#   make -C mmo soundtrack SOUNDTRACK_BW=~/pokeblack.nds
SOUNDTRACK_BASE ?= $(ENGINE)/build/rom/res/sound/pl_sound_data.sdat
soundtrack:
	@if [ -z "$(SOUNDTRACK_HG)" ] && [ -z "$(SOUNDTRACK_BW)" ]; then \
	   echo "soundtrack: SOUNDTRACK_HG=<heartgold.nds> and/or" \
	        "SOUNDTRACK_BW=<black-or-white.nds> is required" >&2; exit 2; \
	 fi
	@mkdir -p $(BUILD)/soundtrack
	@set -e; \
	 rom_for() { case $$1 in pt) echo "";; hg) echo "$(SOUNDTRACK_HG)";; \
	             bw) echo "$(SOUNDTRACK_BW)";; esac; }; \
	 slot_for() { case $$1 in pt) echo platinum;; hg) echo heartgold;; \
	             bw) echo blackwhite;; esac; }; \
	 cp "$(SOUNDTRACK_BASE)" $(BUILD)/soundtrack/fonted-pt.sdat; \
	 for f in hg bw; do \
	   rom=$$(rom_for $$f); [ -n "$$rom" ] || continue; \
	   ENGINE=$(ENGINE) python3 $(ROOT)/tools/soundfont.py compose \
	     "$(SOUNDTRACK_BASE)" "$$rom" $(ROOT)/SOUNDFONTS $$(slot_for $$f) \
	     $(BUILD)/soundtrack/fonted-$$f.sdat; \
	 done; \
	 for t in pt hg bw; do for f in pt hg bw; do \
	   [ "$$t$$f" = "ptpt" ] && continue; \
	   trom=$$(rom_for $$t); from=$$(rom_for $$f); \
	   if { [ "$$t" != pt ] && [ -z "$$trom" ]; } || \
	      { [ "$$f" != pt ] && [ -z "$$from" ]; }; then \
	     echo "soundtrack: sound_$${t}_$${f} skipped (no $$t/$$f image given)"; \
	     continue; \
	   fi; \
	   echo "== sound_$${t}_$${f}"; \
	   refopt=""; \
	   if [ "$$t" != pt ] && [ "$$f" != "$$t" ]; then \
	     refopt="--refont --fonts=$(ROOT)/SOUNDFONTS"; \
	   fi; \
	   ENGINE=$(ENGINE) python3 $(ROOT)/tools/soundtrack.py $$refopt \
	     $(BUILD)/soundtrack/fonted-$$f.sdat "$${trom:--}" \
	     $(ROOT)/SOUNDTRACKS $$(slot_for $$t) \
	     $(ROOT)/mods/sound_$${t}_$${f}/replace/data/sound/pl_sound_data.sdat; \
	 done; done

# The release: the three programs laid out so each one's own default lookup
# finds the next, plus the text saying what a player must supply. Not a
# prerequisite of anything above, it copies what those targets built and
# refuses by name if one of them has not run, so a machine with no engine and
# no SDL2 can still build and test the client.
#
# PACKAGE_HOST is the one thing Makefile.win overrides: same script, same
# allowlist, same manifest, a zip instead of a tarball. `make -f Makefile.win
# package` is the Windows one.
PACKAGE_HOST ?= linux
package:
	@$(ROOT)/package.sh --host $(PACKAGE_HOST) --build $(BUILD) \
	    $(if $(filter 1,$(RELEASE)),--strip)

# Both releases at once, as two zips, each one unpacked and started before it
# counts as written. This is the target for handing the client to a person;
# `package` above is the one for building yourself a folder to run.
dist:
	@$(ROOT)/dist.sh

clean:
	rm -rf $(BUILD)
