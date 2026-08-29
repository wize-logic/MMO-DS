#!/usr/bin/env bash
# Open the game. Builds whatever is missing, checks a server is there, and
# presses Play on the saved settings.
#
#   ./play.sh                         # straight into the game
#   ./play.sh --menu                  # open the front door instead
#   ./play.sh --plan                  # print what Play would run, and stop
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

MODE=play
case "${1:-}" in
    --menu) MODE=menu ;;
    --plan) MODE=plan ;;
    "")     ;;
    *)      echo "usage: $0 [--menu|--plan]" >&2; exit 2 ;;
esac

export DISPLAY="${DISPLAY:-:0}"
export ENGINE_DIR="${ENGINE_DIR:-$PWD/engine/pokeplatinum}"

LAUNCHER=mmo/build/openmmo-launch
VIEWER=mmo/build/openmmo-view
GAME=mmo/build/fused/pokeplatinum

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port="$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)"
GAME_PORT="${OPENMMO_GAMEPORT:-${cfg_port:-7777}}"

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

[[ -d "$ENGINE_DIR" ]] || die "ENGINE_DIR does not exist: $ENGINE_DIR"

# Build only what is missing. Touching a source file and running this again
# will not rebuild on its own; use make when you have just changed code.
need=()
[[ -x "$GAME"     ]] || need+=(fused)
[[ -x "$LAUNCHER" ]] || need+=(launcher)
[[ -x "$VIEWER"   ]] || need+=(viewer)
if (( ${#need[@]} )); then
    say "building: ${need[*]}"
    make -f mmo/Makefile "${need[@]}" -j"$(nproc)" || die "the build failed"
fi

if ! listening "$LOGIN_PORT" || ! listening "$GAME_PORT"; then
    warn "no server on ${LOGIN_PORT}/${GAME_PORT}. Start one with ./start-server.sh"
    warn "carrying on anyway, the front door will report it too"
fi

case "$MODE" in
    plan)
        say "what Play would run"
        exec "./$LAUNCHER" --print-plan
        ;;
    menu)
        say "opening the front door"
        exec "./$LAUNCHER"
        ;;
    *)
        say "playing (login $LOGIN_PORT, game $GAME_PORT)"
        exec "./$LAUNCHER" --play
        ;;
esac
