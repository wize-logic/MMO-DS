#!/bin/sh
# The design notes against the three programs that hold
# settings.
set -eu

ROOT=${1:?usage: settings_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: settings_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: settings_test.sh <mmo-root> <build-dir> <engine-dir>}

PAGE="$ROOT/SETTINGS.md"
if [ ! -f "$PAGE" ]; then
    echo "settings: SKIP (no $PAGE)"
    exit 0
fi
PLAN="$ROOT/launcher/launch_plan.c"
INPUT="$ROOT/viewer/view_input.c"
MOD="$ROOT/mods/openmmo/src/openmmo_input.c"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

echo "the settings page still names what the programs hold:"

# ---------------------------------------------------------------- the front door
# Every key the config parser accepts, and every key the page lists as accepted.
sed -n 's/.*strcmp(key, "\([a-z0-9-]*\)").*/\1/p' "$PLAN" | sort -u > "$tmp/keys.code"
sed -n '/^## What the front door keeps/,/^## /p' "$PAGE" \
    | grep -o '`[a-z0-9-]*`' | tr -d '`' | sort -u > "$tmp/keys.page"

if [ ! -s "$tmp/keys.code" ]; then
    bad "the config parser's keys were readable ($PLAN)"
elif diff -q "$tmp/keys.code" "$tmp/keys.page" > /dev/null 2>&1; then
    ok "every launcher.cfg key is on the page and every listed key is real ($(wc -l < "$tmp/keys.code"))"
else
    bad "every launcher.cfg key is on the page and every listed key is real"
    diff "$tmp/keys.page" "$tmp/keys.code" | sed 's/^/       /'
fi

n=$(sed -n 's/.*`launcher.cfg`, \([0-9]*\) keys.*/\1/p' "$PAGE" | head -1)
if [ "$n" = "$(wc -l < "$tmp/keys.code" | tr -d ' ')" ]; then
    ok "the page's count of them is the count ($n)"
else
    bad "the page's count of them is the count (page $n, code $(wc -l < "$tmp/keys.code"))"
fi

# ---------------------------------------------------------------- the key map
# The window's defaults, as `pad KEY` pairs, out of the table the window starts
# from and out of the page's own column.
sed -n 's/.*{ *SDL_SCANCODE_\([A-Z]*\), *OPENMMO_VIEW_KEY_\([A-Z]*\) *}.*/\2 \1/p' \
    "$INPUT" | sort > "$tmp/map.code"
sed -n '/^| DS button |/,/^$/p' "$PAGE" \
    | awk -F'|' '/^\|/ && $2 !~ /DS button|---/ {
          b = $2; k = $4;
          gsub(/[ *]/, "", b); gsub(/[ *]/, "", k);
          if (k == "arrows") next;
          print toupper(b), toupper(k);
      }' | sort > "$tmp/map.page"

# The directions are one row on the page and four in the table; assert them
# directly rather than teaching the parser to expand a word.
for d in UP DOWN LEFT RIGHT; do
    grep -q "^$d $d\$" "$tmp/map.code" || bad "$d is bound to the $d arrow"
done
grep -q "arrows" "$tmp/map.page" > /dev/null 2>&1 || true
ok "the four directions are the four arrows"

grep -vE '^(UP|DOWN|LEFT|RIGHT) ' "$tmp/map.code" | sort > "$tmp/map.code.rest"
if diff -q "$tmp/map.code.rest" "$tmp/map.page" > /dev/null 2>&1; then
    ok "every other default is the key the page says it is ($(wc -l < "$tmp/map.page") of them)"
else
    bad "every other default is the key the page says it is"
    diff "$tmp/map.page" "$tmp/map.code.rest" | sed 's/^/       /'
fi

# The one binding this project changed away from the port's, spelled out so a
# revert is a failure here rather than a surprise to a player.
if grep -q '^A Z$' "$tmp/map.code" && grep -q '^B X$' "$tmp/map.code"; then
    ok "confirm is Z and cancel is X, which is the official client's way round"
else
    bad "confirm is Z and cancel is X, which is the official client's way round"
fi

# The launcher's Controls panel shows defaults out of its own transcription of
# that table (BIND_PADS), because it cannot link SDL to read the real one; a
# default moved in the window must move there too or the panel shows a lie.
# SDL key names and scancode names differ in shape ("Return" vs RETURN), so
# both sides are compared uppercased.
sed -n '/BIND_PADS\[MMO_LAUNCH_PADS\]/,/^};/p' "$PLAN" \
    | sed -n 's/.*{ *"\([a-z]*\)", *"[^"]*", *"\([^"]*\)" *}.*/\1 \2/p' \
    | tr 'a-z' 'A-Z' | sort > "$tmp/map.launcher"
if [ -s "$tmp/map.launcher" ] && diff -q "$tmp/map.launcher" "$tmp/map.code" > /dev/null 2>&1; then
    ok "the Controls panel's defaults are the window's ($(wc -l < "$tmp/map.launcher") pads)"
else
    bad "the Controls panel's defaults are the window's"
    diff "$tmp/map.code" "$tmp/map.launcher" 2>&1 | sed 's/^/       /'
fi

# ---------------------------------------------------------------- button mode
# Three names, spelled the same in the config file, in the game and on the page.
sed -n 's/.*button_names\[MMO_BUTTON_N\] *= *{ *\(.*\) *};.*/\1/p' "$PLAN" \
    | tr -d '" ' | tr ',' '\n' | grep . | sort > "$tmp/modes.launcher"
sed -n 's/.*{ *"\([a-z-]*\)", *OPTIONS_BUTTON_MODE_.*/\1/p' "$MOD" | sort > "$tmp/modes.mod"

if [ -s "$tmp/modes.launcher" ] && diff -q "$tmp/modes.launcher" "$tmp/modes.mod" > /dev/null 2>&1; then
    ok "the front door and the game spell the button modes the same ($(tr '\n' ' ' < "$tmp/modes.mod"))"
else
    bad "the front door and the game spell the button modes the same"
    diff "$tmp/modes.launcher" "$tmp/modes.mod" 2>&1 | sed 's/^/       /'
fi

missing=
for m in $(cat "$tmp/modes.mod"); do
    grep -q "\`$m\`" "$PAGE" || missing="$missing $m"
done
if [ -z "$missing" ]; then
    ok "the page names each of them"
else
    bad "the page names each of them (missing:$missing)"
fi

# And that the save is no longer written: the patch is what makes that true, so
# a patch that stops carrying this hunk is the regression.
PATCHFILE="$ROOT/mods/openmmo/patches/src/game_options.c.patch"
if grep -q '^-    options->buttonMode = mode;' "$PATCHFILE" 2>/dev/null &&
   grep -q '^-    return options->buttonMode;' "$PATCHFILE" 2>/dev/null; then
    ok "the save's two button-mode bits are neither read nor written by the game"
else
    bad "the save's two button-mode bits are neither read nor written by the game"
fi

# ---------------------------------------------------------------- the engine's rows
OPTH="$ENGINE/include/game_options.h"
CONSTH="$ENGINE/include/constants/game_options.h"
if [ ! -f "$OPTH" ] || [ ! -f "$CONSTH" ]; then
    echo "  SKIP the engine's own option list (no checkout at $ENGINE)"
else
    sed -n '/typedef struct Options/,/} Options;/p' "$OPTH" \
        | sed -n 's/^ *u16 \([a-zA-Z][a-zA-Z]*\) *:.*/\1/p' | sort > "$tmp/opts.engine"
    sed -n '/^| row | field |/,/^$/p' "$PAGE" \
        | awk -F'|' '/^\|/ && $3 !~ /field|---/ { gsub(/[ `*]/, "", $3); if ($3 != "") print $3 }' \
        | sort > "$tmp/opts.page"
    if diff -q "$tmp/opts.engine" "$tmp/opts.page" > /dev/null 2>&1; then
        ok "the options screen still has the six rows the page assigns owners to"
    else
        bad "the options screen still has the six rows the page assigns owners to"
        diff "$tmp/opts.page" "$tmp/opts.engine" | sed 's/^/       /'
    fi

    # The engine's enum is the ceiling on what a button mode may be. A fourth
    # one appearing means the launcher is offering fewer than the game has.
    e=$(grep -c 'OPTIONS_BUTTON_MODE_' "$CONSTH" || true)
    if [ "$e" = "3" ]; then
        ok "the game has exactly the three button modes the front door offers"
    else
        bad "the game has exactly the three button modes the front door offers (it has $e)"
    fi
fi

# ---------------------------------------------------------------- the measurement
FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
CONT="$ENGINE/pc/replays/lab-continue.txt"

if [ ! -x "$FUSED" ]; then
    echo "  SKIP the button mode measured in the game (no fused build: run \`make -C mmo fused\`)"
elif [ ! -f "$ROM" ] || [ ! -f "$CONT" ]; then
    echo "  SKIP the button mode measured in the game (no ROM or input script under $ENGINE)"
else
    echo "and the game takes its button mode from the host rather than the save:"

    # One save, minted by the lab on a new game, then booted through the title
    # by continue, which is the path the engine applies a button mode on.
    printf 'name TEST\n' > "$tmp/min.lab"
    OPENMMO_ASSERT=warn PC_ROM="$ROM" PC_SAVE="$tmp/base.sav" PC_LAB="$tmp/min.lab" \
        PC_LAB_AT=1800 PC_FRAMES=2600 PC_PACE=0 \
        PC_INPUT="$ENGINE/pc/replays/lab-settle.txt" "$FUSED" > "$tmp/mint.log" 2>&1 || {
        bad "a save was minted to boot from (exit $?)"; sed -n '$p' "$tmp/mint.log"; }

    # X opens the start menu; the key under test is pressed into it, where A
    # selects a row and draws something else entirely.
    for k in none A L; do
        cp "$CONT" "$tmp/in-$k.txt"
        printf '2900 keys X\n2916 keys none\n' >> "$tmp/in-$k.txt"
        [ "$k" = none ] || printf '3100 keys %s\n3116 keys none\n' "$k" >> "$tmp/in-$k.txt"
    done

    shot() { # mode key -> digest of frame 3400
        cp "$tmp/base.sav" "$tmp/run.sav"
        env OPENMMO_ASSERT=warn OPENMMO_BUTTON_MODE="$1" PC_ROM="$ROM" \
            PC_SAVE="$tmp/run.sav" PC_FRAMES=3400 PC_PACE=0 PC_INPUT="$tmp/in-$2.txt" \
            PC_DUMP_FRAMES="$tmp/d-$1-$2" PC_DUMP_FROM=3400 "$FUSED" \
            > "$tmp/$1-$2.log" 2>&1 || echo "boot $1/$2 exited $?" >&2
        awk '$1=="003400"{print $NF}' "$tmp/d-$1-$2/frames.txt" 2>/dev/null
    }

    idle=$(shot normal none)
    conf=$(shot normal A)
    plain=$(shot normal L)
    lisa=$(shot l-is-a L)

    if [ -z "$idle" ] || [ -z "$conf" ]; then
        bad "the four boots each drew frame 3400 (idle '$idle', A '$conf')"
    elif [ "$idle" = "$conf" ]; then
        bad "pressing A into the start menu draws something else than pressing nothing"
    else
        ok "pressing A into the start menu draws something else than pressing nothing"
        if [ "$plain" = "$idle" ]; then
            ok "with no button mode set, L does nothing at all"
        else
            bad "with no button mode set, L does nothing at all ($plain vs $idle)"
        fi
        if [ "$lisa" = "$conf" ]; then
            ok "with l-is-a from the host, L draws exactly what A drew"
        else
            bad "with l-is-a from the host, L draws exactly what A drew ($lisa vs $conf)"
        fi
    fi
fi

# ---------------------------------------------------------------- the server
# The one thing on this page that is deliberately not a setting.
echo "and the server is not one of them:"

PINHDR="$BUILD/gen/endpoint_pin.h"
CLIENTBIN="$BUILD/openmmo-client"

if [ ! -f "$PINHDR" ] || [ ! -x "$CLIENTBIN" ]; then
    echo "  SKIP (no built client: run \`make -C mmo\`)"
else
    pinhost=$(sed -n 's/^#define OPENMMO_PIN_HOST  *"\(.*\)"$/\1/p' "$PINHDR")
    if [ -z "$pinhost" ]; then
        bad "the build says which server it is for"
    elif strings "$CLIENTBIN" | grep -qF "$pinhost"; then
        bad "the address is not printable in the client ($pinhost is)"
    else
        ok "the address the client dials is not printable in it"
    fi

    # The key is the opposite case to the address: public by nature, so it is
    # Not obfuscated, and the check is that the bytes the build pinned are the
    # bytes the program carries. A client built for one server with another
    # server's key fails at the last line of the handshake and nowhere else,
    # which is a whole release wasted, so the pin is read back out of .rodata
    # rather than trusted because the header said so.
    pinkey=$(sed -n 's/^#define OPENMMO_PIN_ROOT_KEY  *//p' "$PINHDR" |
                 tr -d ' ,' | sed 's/0x//g')
    if [ -z "$pinkey" ]; then
        bad "the build says which root key it trusts"
    elif [ "${#pinkey}" -ne 130 ]; then
        bad "the pinned root key is 65 bytes (it is $((${#pinkey} / 2)))"
    elif od -An -tx1 -v "$CLIENTBIN" | tr -d ' \n' | grep -qF "$pinkey"; then
        ok "the client carries the root key the build pinned"
    else
        bad "the client carries the root key the build pinned (${pinkey%${pinkey#????????}}... is not in it)"
    fi

    if "$CLIENTBIN" 2>&1 | grep -qe '--host' -e '--gameport'; then
        bad "the client offers no way to be pointed at another server"
    else
        ok "the client offers no way to be pointed at another server"
    fi
fi

# ---------------------------------------------------------------- the doors
# The other things on this page that stop being settings in a shipped build.
echo "and a development build's doors are shut in a release:"

DOORSRC=$(echo "$ROOT"/mods/openmmo/src/*.c)
PLAIN='OPENMMO_SESSION|OPENMMO_OFFLINE|OPENMMO_USER|OPENMMO_PASS|OPENMMO_CHARACTER|OPENMMO_EXPORT|OPENMMO_EXPORT_MONEY|OPENMMO_IMPORT|OPENMMO_IMPORT_CHAIN|OPENMMO_REVISION|OPENMMO_BUTTON_MODE|OPENMMO_MUSIC|OPENMMO_SFX|OPENMMO_DISCORD|OPENMMO_CAMERA_DISTANCE|OPENMMO_HUD|OPENMMO_APPEARANCE_DUMP|OPENMMO_INTERACT_REPORT|OPENMMO_PEER_REPORT|OPENMMO_SETTLE_TRACE|OPENMMO_BATTLE_TRACE|OPENMMO_HEAP_REPORT|OPENMMO_INPUT_REPORT|OPENMMO_CLOCK_REPORT|OPENMMO_NAME_REPORT|OPENMMO_LABEL_REPORT|OPENMMO_FONT_REPORT|OPENMMO_SPRITE_DUMP'

names() { grep -ho "$1(\"OPENMMO_[A-Z_0-9]*\"" $DOORSRC | sed 's/.*"\(.*\)"/\1/' | sort -u; }

hidden=$(grep -Hn 'getenv( *[A-Za-z_]' $DOORSRC || true)
if [ -n "$hidden" ]; then
    bad "every environment read in a mod source names the variable at the read"
    echo "$hidden" | sed "s|^$ROOT/|       |"
else
    ok "every environment read in a mod source names the variable at the read"
fi

# Non-OPENMMO reads are the port's own host settings (PC_SAVE, PC_VIEW and the
# rest), which belong to the engine and are not this project's to gate.
foreign=$(grep -ho 'getenv("[A-Za-z_0-9]*"' $DOORSRC | sed 's/.*"\(.*\)"/\1/' \
              | sort -u | grep -Ev '^(OPENMMO|PC)_' || true)
if [ -n "$foreign" ]; then
    bad "a mod source reads only its own variables and the engine's PC_ ones"
    echo "       neither: $(echo $foreign)"
else
    ok "a mod source reads only its own variables and the engine's PC_ ones"
fi

stray=$(names getenv | grep -Ev "^($PLAIN)\$" || true)
if [ -n "$stray" ]; then
    bad "every variable a mod source reads is plumbing, a report, or a dev door"
    echo "       still at plain getenv: $(echo $stray)"
else
    ok "every variable a mod source reads is plumbing, a report, or a dev door"
fi

# A name on the list that nothing reads any more is the list rotting: the next
# person to add a door reads it as precedent for leaving one open.
gone=
for v in $(echo "$PLAIN" | tr '|' ' '); do
    names getenv | grep -q "^$v\$" || gone="$gone $v"
done
if [ -z "$gone" ]; then
    ok "each of the $(echo "$PLAIN" | tr '|' '\n' | wc -l | tr -d ' ') on that list is still read"
else
    bad "each name on that list is still read (nothing reads:$gone)"
fi

doors=$(names openmmo_dev_env)
ndoors=$(printf '%s\n' "$doors" | grep -c . || true)
both=$(printf '%s\n' "$doors" | grep -E "^($PLAIN)\$" || true)
if [ "$ndoors" -lt 43 ]; then
    bad "the doors are still gated ($ndoors found, and there were 43)"
elif [ -n "$both" ]; then
    bad "no variable is both a door and not one ($(echo $both) is both)"
else
    ok "$ndoors of them are doors, and none of them is also on the plain list"
fi

# The rest: a name that reaches neither call because a helper in its own file
# reads it. That is allowed only where the helper reads a door, so the file
# holding the name has to have openmmo_dev_env in it somewhere.
ungated=
for v in $(grep -rho '"OPENMMO_[A-Z_0-9]*"' $DOORSRC | tr -d '"' | sort -u); do
    if printf '%s\n' "$doors" | grep -q "^$v\$"; then continue; fi
    if echo "$v" | grep -qE "^($PLAIN)\$"; then continue; fi
    for f in $(grep -l "\"$v\"" $DOORSRC); do
        grep -q 'openmmo_dev_env(' "$f" || ungated="$ungated $v"
    done
done
if [ -z "$ungated" ]; then
    ok "a name read through a helper is read through one that gates it"
else
    bad "a name read through a helper is read through one that gates it"
    echo "       ungated:$(echo $ungated)"
fi

# And the part the source cannot assert: that the gate answers NULL when the
# build is a release.
if [ ! -f "$PINHDR" ]; then
    echo "  SKIP (no pin header: run \`make -C mmo\`)"
else
    mkdir -p "$tmp/gen"
    sed 's/\(OPENMMO_PIN_DEV_FEATURES\) *1/\1 0/' "$PINHDR" >"$tmp/gen/endpoint_pin.h"
    cat >"$tmp/doors.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "endpoint.h"

int main(void)
{
    setenv("OPENMMO_DEBUG_MENU", "1", 1);
    printf("dev_features %d dev_env %s getenv %s\n",
           openmmo_dev_features(),
           openmmo_dev_env("OPENMMO_DEBUG_MENU") == NULL ? "null" : "set",
           getenv("OPENMMO_DEBUG_MENU") == NULL ? "null" : "set");
    return 0;
}
EOF
    if ${CC:-cc} -o "$tmp/doors" "$tmp/doors.c" "$ROOT/src/devenv.c" \
         -I"$tmp/gen" -I"$ROOT/include" >"$tmp/doors.log" 2>&1; then
        # The notice the gate prints comes first; the verdict is the last line.
        got=$("$tmp/doors" | sed -n '$p')
        if [ "$got" = "dev_features 0 dev_env null getenv set" ]; then
            ok "a release build reads a door as unset with the variable plainly set"
        else
            bad "a release build reads a door as unset with the variable plainly set"
            echo "       said: $got"
        fi
    else
        bad "the release-side probe compiles"
        sed -n '1,5p' "$tmp/doors.log"
    fi
fi

# ------------------------------------------------- the doors outside the game
# The same three kinds again, for the three programs that are not the game:
# the client (src/), the front door (launcher/) and the window (viewer/).
echo "and the same holds outside the game:"

DOORSRC2=$(echo "$ROOT"/src/*.c "$ROOT"/launcher/*.c "$ROOT"/viewer/*.c)
PLAIN2='OPENMMO_ROOT|OPENMMO_ROMS|OPENMMO_PORT|OPENMMO_VIEWER|OPENMMO_LOGS|OPENMMO_THEME|OPENMMO_UI_FONT|OPENMMO_VIEW_LOG_DIR|OPENMMO_VIEW_TRACE|OPENMMO_VIEW_FRAMES|OPENMMO_VIEW_PACE_REPORT|OPENMMO_VIEW_NO_AUDIO|OPENMMO_REVISION'

names2() { grep -ho "$1(\"OPENMMO_[A-Z_0-9]*\"" $DOORSRC2 | sed 's/.*"\(.*\)"/\1/' | sort -u; }

# The hidden-name rule the mod sources keep verbatim cannot hold here: three
# reads take a name that arrives in a variable on purpose, and each of the three
# is handed a name that is not one of ours. So they are named instead, and a
# Fourth is the failure, that one would be a door no grep on this page finds.
grep -Hn 'getenv( *[A-Za-z_]' $DOORSRC2 \
    | sed "s|^$ROOT/||; s/:[0-9][0-9]*:[[:space:]]*/ /" | sort > "$tmp/var.code"
cat > "$tmp/var.want" <<'EOF'
src/devenv.c const char *v = getenv(name);
src/platform.c const char *had = getenv(name);
src/presence.c base = kIpcDirEnv[dir] != NULL ? getenv(kIpcDirEnv[dir]) : kIpcDirs[dir];
EOF
if diff -q "$tmp/var.code" "$tmp/var.want" > /dev/null 2>&1; then
    ok "the three reads that take a name in a variable are the three that may"
else
    bad "the three reads that take a name in a variable are the three that may"
    diff "$tmp/var.want" "$tmp/var.code" | sed 's/^/       /'
fi

# The two address overrides are read with a plain getenv and are neither
# plumbing nor a door: `#if OPENMMO_PIN_SETTABLE` takes them out of a release
# entirely, which is measured at the end of this section rather than listed.
stray2=$(names2 getenv | grep -Ev "^($PLAIN2|OPENMMO_SERVER|OPENMMO_GAMEPORT)\$" || true)
if [ -z "$stray2" ]; then
    ok "every variable these three read plainly is the install's, the window's or a report"
else
    bad "every variable these three read plainly is the install's, the window's or a report"
    echo "       still at plain getenv: $(echo $stray2)"
fi

gone2=
for v in $(echo "$PLAIN2" | tr '|' ' '); do
    names2 getenv | grep -q "^$v\$" || gone2="$gone2 $v"
done
if [ -z "$gone2" ]; then
    ok "each of the $(echo "$PLAIN2" | tr '|' '\n' | wc -l | tr -d ' ') on that list is still read"
else
    bad "each name on that list is still read (nothing reads:$gone2)"
fi

doors2=$(names2 openmmo_dev_env)
ndoors2=$(printf '%s\n' "$doors2" | grep -c . || true)
both2=$(printf '%s\n' "$doors2" | grep -E "^($PLAIN2)\$" || true)
if [ "$ndoors2" -lt 5 ]; then
    bad "the doors outside the game are still gated ($ndoors2 found, and there were 5)"
elif [ -n "$both2" ]; then
    bad "no variable is both a door and not one ($(echo $both2) is both)"
else
    ok "$ndoors2 of them are doors, and none of them is also on the plain list"
fi

# Everything else spelled OPENMMO_ in those three: the front door writes most of
# the game's plumbing into the child's environment, and a name it only writes is
# not a door of its own. A name that is neither read nor written is one this
# check has lost track of.
wrote=$(grep -ho 'setenv("OPENMMO_[A-Z_0-9]*"\|push_env(p, "OPENMMO_[A-Z_0-9]*"' $DOORSRC2 \
            | sed 's/.*"\(.*\)"/\1/' | sort -u)
lost=
for v in $(grep -rho '"OPENMMO_[A-Z_0-9]*"' $DOORSRC2 | tr -d '"' | sort -u); do
    if printf '%s\n' "$doors2" | grep -q "^$v\$"; then continue; fi
    if echo "$v" | grep -qE "^($PLAIN2)\$"; then continue; fi
    if printf '%s\n' "$wrote" | grep -q "^$v\$"; then continue; fi
    # The two the build compiles out entirely; measured below rather than listed.
    case $v in OPENMMO_SERVER|OPENMMO_GAMEPORT) continue;; esac
    lost="$lost $v"
done
if [ -z "$lost" ]; then
    ok "every other OPENMMO_ name in them is one the front door writes for the game"
else
    bad "every other OPENMMO_ name in them is one the front door writes for the game"
    echo "       neither read nor written:$lost"
fi

# And the two that are not gated at run time because they are not COMPILED IN:
# the address overrides.
if [ ! -f "$PINHDR" ]; then
    echo "  SKIP the address overrides (no pin header: run \`make -C mmo\`)"
else
    mkdir -p "$tmp/gen2"
    settable_obj() { # 0|1 -> object path, or empty
        sed "s/\(OPENMMO_PIN_SETTABLE\) *[01]/\1 $1/" "$PINHDR" > "$tmp/gen2/endpoint_pin.h"
        ${CC:-cc} -c -o "$tmp/ep-$1.o" "$ROOT/src/endpoint.c" \
            -I"$tmp/gen2" -I"$ROOT/include" >"$tmp/ep-$1.log" 2>&1 || return 1
        echo "$tmp/ep-$1.o"
    }
    on=$(settable_obj 1) && off=$(settable_obj 0) || on=
    if [ -z "$on" ]; then
        bad "the address overrides compile both ways"
        sed -n '1,5p' "$tmp/ep-1.log" "$tmp/ep-0.log" 2>/dev/null
    else
        got=
        for v in OPENMMO_SERVER OPENMMO_GAMEPORT; do
            if ! strings "$on" | grep -q "$v"; then
                got="$got $v-missing-from-a-dev-build"
            fi
            if strings "$off" | grep -q "$v"; then
                got="$got $v-still-in-a-release"
            fi
        done
        if [ -z "$got" ]; then
            ok "the address overrides are in a build from this tree and in no release"
        else
            bad "the address overrides are in a build from this tree and in no release"
            echo "      $got"
        fi
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "settings: all checks passed"
else
    echo "settings: FAILED"
fi
exit "$fail"
