#!/bin/sh
# The poketch mods are gone, and stay gone.
#
#   1. the device is the story's to switch on, the seat enables it only for
#      a character the server says was handed one, and `ScrCmd_131` stands
#      down when one is already running (running it over a live PoketchSystem
#      destroys the heap that system holds, which is the Jubilife crash);
#   2. no mod source seats an app on it, and no hook patch exists to;
#   3. the window hides the second screen whenever the guest says the
#      Poketch would be on it, draws nothing there, stops growing the
#      band for its pen, and publishes no touch onto the hidden screen;
#   4. and the guest says so from the field, not from the device: asking
#      whether a PoketchSystem was live is what made the enable load-bearing.
set -eu

ROOT=${1:?usage: poketch_test.sh <mmo-root> [build-dir] [engine-dir]}

POK="$ROOT/mods/openmmo/src/openmmo_poketch.c"
HUD="$ROOT/mods/openmmo/src/openmmo_hud.c"
SCR="$ROOT/mods/openmmo/patches/src/scrcmd.c.patch"
VIEW="$ROOT/viewer/viewer.c"

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

echo "the poketch mods are gone, the hide remains:"

if [ -f "$POK" ] && grep -q 'FLAG_RECEIVED_POKETCH' "$POK" \
        && grep -q 'Poketch_Enable' "$POK"; then
    ok "the device is switched on only where the story handed one over"
else
    bad "the device is switched on only where the story handed one over" \
        "openmmo_poketch.c must gate Poketch_Enable on FLAG_RECEIVED_POKETCH"
fi

if [ -f "$POK" ] && ! grep -q 'SystemFlag_ClearPoketchHidden' "$POK"; then
    ok "the seat no longer forces the device on"
else
    bad "the seat no longer forces the device on" \
        "an unconditional enable is what makes ScrCmd_131 crash in Jubilife"
fi

if [ -f "$SCR" ] && grep -q 'FieldSystem_GetPoketchSystem() != NULL' "$SCR"; then
    ok "the switch-on command stands down on a running device"
else
    bad "the switch-on command stands down on a running device" \
        "scrcmd.c.patch must guard ScrCmd_131; it Heap_Destroys HEAP_ID_POKETCH_APP"
fi

if [ -f "$HUD" ] && ! grep -q 'FieldSystem_GetPoketchSystem' "$HUD"; then
    ok "the guest reads the field, not the device, for its lower screen"
else
    bad "the guest reads the field, not the device, for its lower screen" \
        "lower_now() must not ask whether a PoketchSystem is running"
fi

if ! grep -rq 'PoketchSystem_SetAppFunctions' "$ROOT/mods/openmmo/src"; then
    ok "no mod source seats an app on the device"
else
    bad "no mod source seats an app on the device" \
        "$(grep -rl 'PoketchSystem_SetAppFunctions' "$ROOT/mods/openmmo/src")"
fi

if [ ! -e "$ROOT/mods/openmmo/patches/src/applications/poketch/poketch_system.c.patch" ]; then
    ok "the PoketchSystem_InitApp hook patch is gone"
else
    bad "the PoketchSystem_InitApp hook patch is gone" \
        "the patch would re-point apps nothing provides any more"
fi

if ! grep -rq 'OPENMMO_POKETCH' "$ROOT/mods/openmmo/src" "$ROOT/src"; then
    ok "the app-select env is gone with the apps"
else
    bad "the app-select env is gone with the apps" \
        "$(grep -rln 'OPENMMO_POKETCH' "$ROOT/mods/openmmo/src" "$ROOT/src")"
fi

if grep -q 'viewer_hides_second' "$VIEW" \
        && grep -A 8 'static void viewer_blit_guest' "$VIEW" \
           | grep -q 'viewer_hides_second'; then
    ok "the window draws no second screen while the guest would show the Poketch"
else
    bad "the window draws no second screen while the guest would show the Poketch" \
        "viewer_blit_guest must stand down on viewer_hides_second"
fi

if grep -q 'touch_wanted && !viewer_hides_second' "$VIEW"; then
    ok "a hidden screen never grows the band for its pen"
else
    bad "a hidden screen never grows the band for its pen" \
        "the sec ramp must gate touch_wanted on the screen being visible"
fi

[ "$fail" -eq 0 ] && echo "poketch: ok" || echo "poketch: FAIL"
exit "$fail"
