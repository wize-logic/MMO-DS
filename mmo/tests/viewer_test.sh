#!/bin/sh
# The window and the front door, driven with nobody at the
# machine.
set -u

ROOT="${1:?usage: viewer_test.sh <mmo-root> <build-dir>}"
BUILD="${2:?usage: viewer_test.sh <mmo-root> <build-dir>}"

VIEW="$BUILD/openmmo-view"
LAUNCH="$BUILD/openmmo-launch"
DRIVE="$BUILD/openmmo-viewdrive"

for f in "$VIEW" "$LAUNCH" "$DRIVE"; do
    if [ ! -x "$f" ]; then
        echo "viewer: SKIP (no $(basename "$f"), build it with 'make viewer launcher')"
        exit 0
    fi
done

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/openmmo-viewer-XXXXXX")
CHAN="/openmmo-viewtest-$$"
cleanup() { "$DRIVE" unlink "$CHAN" >/dev/null 2>&1; rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

SDL_VIDEODRIVER=dummy
export SDL_VIDEODRIVER

# One window, headless, over whatever the page currently holds. Prints the
# viewer's own exit status; its stderr is kept for the checks that read it.
present() {
    shot="$1"
    shift
    # The live-shot oracle is the smart layout at scale 2, nearest, render
    # scale 1, the picture view_drive.c counts. fill / --scale auto / sharp-
    # bilinear are the window's own defaults now, and are not this pin.
    timeout 60 "$VIEW" "$CHAN" --no-audio --layout smart --scale 2 \
        --render-scale 1 --filter nearest --shot "$shot" "$@" \
        >"$TMP/out" 2>"$TMP/err"
    echo $?
}

echo "the window presents what the game published, with nobody watching:"

"$DRIVE" publish "$CHAN" || bad "the synthetic publisher could not create a page"
rc=$(present "$TMP/live.ppm" --wait 10000)
if [ "$rc" = 0 ] && [ -f "$TMP/live.ppm" ]; then
    ok "a published frame is presented and written out"
    "$DRIVE" shotcheck "$TMP/live.ppm" live || fails=$((fails + 1))
else
    bad "the window did not present a live page (exit $rc)"
    sed 's/^/       /' "$TMP/err"
fi

# The one failure the frame protocol names by name. Asserted three ways: the
# window says it, exits differently for it, and does not draw the frame.
"$DRIVE" publish --version 999 "$CHAN"
rc=$(present "$TMP/bad.ppm" --wait 10000)
if [ "$rc" = 3 ]; then
    ok "a page of the wrong version is refused, and says so in its exit status"
else
    bad "a version mismatch exited $rc, not 3"
fi
if grep -q 'VERSION MISMATCH' "$TMP/err"; then
    ok "the refusal names the mismatch and both version numbers"
else
    bad "the refusal did not name the version mismatch: $(cat "$TMP/err")"
fi
[ -f "$TMP/bad.ppm" ] && "$DRIVE" shotcheck "$TMP/bad.ppm" refused \
    || bad "no picture was drawn for the refusal"

# The publisher stores the magic last, so there is a moment when the page exists
# and is not yet one of ours. A window that looks in that moment must wait, not
# refuse: refusing there kills the window of a session whose game is starting
# normally.
"$DRIVE" publish --no-magic "$CHAN"
( sleep 2; "$DRIVE" stamp "$CHAN" ) &
stamper=$!
rc=$(present "$TMP/late.ppm" --wait 20000)
wait "$stamper" 2>/dev/null
if [ "$rc" = 0 ]; then
    ok "a page created but not yet stamped is waited for, not refused"
    "$DRIVE" shotcheck "$TMP/late.ppm" live || fails=$((fails + 1))
else
    bad "a page stamped a moment late was not waited for (exit $rc)"
    sed 's/^/       /' "$TMP/err"
fi

# And the same page when nothing ever fills it in: a failure on the wait, told
# apart from an empty name by its exit status.
"$DRIVE" publish --no-magic "$CHAN"
rc=$(present "$TMP/never.ppm" --wait 1000)
[ "$rc" = 3 ] && ok "a page nobody fills in fails once the wait runs out" \
               || bad "an unstamped page exited $rc, not 3"

"$DRIVE" unlink "$CHAN"
rc=$(present "$TMP/none.ppm" --wait 1000)
[ "$rc" = 2 ] && ok "no channel at all is a different failure from a bad one" \
               || bad "a missing channel exited $rc, not 2"

# The player's half, through the same page.
"$DRIVE" input "$CHAN" || fails=$((fails + 1))

echo "the front door decides a launch without a screen:"

CFG="$TMP/config"
mkdir -p "$CFG/openmmo"
XDG_CONFIG_HOME="$CFG"
export XDG_CONFIG_HOME

# The launcher adopts $PC_ROM or $ENGINE_DIR/build/rom when the settings
# name no ROM at all (a built tree has none beside the binary). make test
# inherits ENGINE_DIR from the loop, so "nothing set up" has to drop both.
if out=$(env -u ENGINE_DIR -u PC_ROM "$LAUNCH" --print-plan 2>&1); then
    bad "a launcher with no ROM printed a plan anyway: $out"
elif printf '%s' "$out" | grep -q 'ROM'; then
    ok "with nothing set up, the plan is refused and names what is missing"
else
    bad "the refusal did not name the ROM: $out"
fi

: > "$TMP/game.nds"
{
    echo "rom $TMP/game.nds"
    echo "user test"
    echo "scale 3"
} > "$CFG/openmmo/launcher.cfg"

if out=$("$LAUNCH" --print-plan 2>&1); then
    miss=""
    for want in "chan " "env PC_VIEW=" "env PC_ROM=$TMP/game.nds" \
                "env OPENMMO_SESSION=1" "env PC_MODS_DIR=" \
                "port " "view "; do
        printf '%s\n' "$out" | grep -qF "$want" || miss="$miss [$want]"
    done
    [ -z "$miss" ] && ok "the plan names the channel, the game's environment and both command lines" \
                   || bad "the plan is missing:$miss"
    printf '%s\n' "$out" | grep -q 'view .*--scale 3' \
        && ok "a setting the player changed reaches the window's own arguments" \
        || bad "the plan did not carry --scale 3 to the window"
    chan=$(printf '%s\n' "$out" | sed -n 's/^chan //p')
    printf '%s\n' "$out" | grep -qF "env PC_VIEW=$chan" \
        && printf '%s\n' "$out" | grep -qF "view " \
        && ok "the game and the window are given the same channel name" \
        || bad "the two halves of the plan do not name one channel"
else
    bad "a configured launcher would not print a plan: $out"
fi

if timeout 60 "$LAUNCH" --shot "$TMP/menu.ppm" >"$TMP/lout" 2>&1 \
   && [ -s "$TMP/menu.ppm" ]; then
    ok "the menu draws headless, with no display and no keypress"
else
    bad "the launcher drew no menu: $(cat "$TMP/lout")"
fi

# The menu is a picture of the settings, not a template drawn beside them: the
# same window with a different account in it is a different picture.
sed 's/^user test$/user someone-else/' "$CFG/openmmo/launcher.cfg" \
    > "$TMP/other.cfg"
cp "$TMP/other.cfg" "$CFG/openmmo/launcher.cfg"
if timeout 60 "$LAUNCH" --shot "$TMP/menu2.ppm" >"$TMP/lout" 2>&1 \
   && [ -s "$TMP/menu2.ppm" ] && ! cmp -s "$TMP/menu.ppm" "$TMP/menu2.ppm"; then
    ok "the menu draws the settings it holds, not a fixed picture"
else
    bad "the menu did not change when the account did"
fi

# The same keys a person presses, without a display: --script feeds the
# handler the window's event loop calls. Reset to scale 2 so the right arrow
# is a real change.
{
    echo "rom $TMP/game.nds"
    echo "server 127.0.0.1:2106"
    echo "user someone-else"
    echo "scale 2"
} > "$CFG/openmmo/launcher.cfg"
timeout 60 "$LAUNCH" --shot "$TMP/menu2b.ppm" >"$TMP/lout" 2>&1 || true
downs=$(awk '
    /^static const struct launch_row rows\[/ { on = 1; next }
    on && /"SCALE"/                            { print n; exit }
    on && /^[ \t]*\{ "/                        { n++ }
' "$ROOT/launcher/launch_menu.c")
case "$downs" in
    ''|*[!0-9]*) bad "the launcher still has a SCALE row"; downs=0;;
esac
: > "$TMP/keys.txt"
i=0
while [ "$i" -lt "$downs" ]; do echo down >> "$TMP/keys.txt"; i=$((i + 1)); done
echo right >> "$TMP/keys.txt"
if timeout 60 "$LAUNCH" --script "$TMP/keys.txt" --shot "$TMP/menu3.ppm" \
        >"$TMP/lout" 2>&1 \
   && [ -s "$TMP/menu3.ppm" ] && [ -s "$TMP/menu2b.ppm" ] \
   && ! cmp -s "$TMP/menu2b.ppm" "$TMP/menu3.ppm"; then
    ok "a script of menu keys changes the picture the way a keypress would"
else
    bad "menu keys did not change the picture: $(cat "$TMP/lout")"
fi
if grep -q '^scale 3$' "$CFG/openmmo/launcher.cfg"; then
    ok "those keys wrote the setting Play would launch with"
else
    bad "the scripted keys did not save scale 3"
    sed -n '/^scale /p' "$CFG/openmmo/launcher.cfg" | sed 's/^/       /'
fi

echo "the window opens a sound device when the front door leaves sound on:"
# Dummy driver so a runner with no speakers still checks the path. A real
# device is measured live, through the launcher, against Pulse. SKIP rather
# than fail if this SDL cannot init audio at all.
"$DRIVE" publish "$CHAN"
"$DRIVE" feed-audio "$CHAN" 4000 >/dev/null 2>&1 &
feed=$!
SDL_AUDIODRIVER=dummy
export SDL_AUDIODRIVER
timeout 15 "$VIEW" "$CHAN" --shot "$TMP/audio.ppm" --wait 8000 \
    >"$TMP/aout" 2>"$TMP/aerr" || true
kill "$feed" 2>/dev/null
wait "$feed" 2>/dev/null
if grep -q 'audio at ' "$TMP/aerr"; then
    ok "sound on opens a device and says so ($(sed -n 's/.*audio at /at /p' "$TMP/aerr" | head -1))"
elif grep -q 'no audio here\|cannot open an audio device' "$TMP/aerr"; then
    echo "  skip sound: this SDL has no audio device ($(tr '\n' ' ' < "$TMP/aerr"))"
else
    bad "sound on said nothing about a device"
    sed 's/^/       /' "$TMP/aerr"
fi

# An unknown setting is refused rather than dropped: a config the launcher
# silently ignored is a player whose settings did nothing.
echo "wobble 1" >> "$CFG/openmmo/launcher.cfg"
if out=$("$LAUNCH" --print-plan 2>&1); then
    bad "an unknown setting was accepted: $out"
else
    printf '%s' "$out" | grep -q 'wobble' \
        && ok "an unknown setting is refused by name and line" \
        || bad "the refusal did not name the setting: $out"
fi

if [ "$fails" -gt 0 ]; then
    echo "viewer: $fails check(s) FAILED"
    exit 1
fi
echo "viewer: all checks passed"
