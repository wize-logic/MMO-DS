#!/usr/bin/env bash
# mmo/play.sh, start the fused OpenMMO client in a window.
#
#   ./mmo/play.sh                       # boot to the port's own title, no session
#   ./mmo/play.sh --online              # OpenMMO title, then join the game
#   ./mmo/play.sh --online --world      # skip the title, into a connected map
#   ./mmo/play.sh --server HOST[:PORT]  # --online, but at a different address.
#                                       # which server a client dials is compiled
#                                       # in (mmo/Makefile SERVER_HOST); this
#                                       # only does anything in a build from this
#                                       # tree, where SERVER_SETTABLE is 1
#   ./mmo/play.sh --user U --pass P     # log that session in as U/P (an account you made; the
#                                       # server seeds none)
#   ./mmo/play.sh --character NAME      # pick that row; empty takes the first
#   ./mmo/play.sh --gameport G          # override the game port (same seam as
#                                       # --server: a development build only)
#   ./mmo/play.sh --save FILE           # play a particular save
#   ./mmo/play.sh --new                 # erase the default save first
#   ./mmo/play.sh --rom FILE            # the ROM to memory-map ($PC_ROM otherwise)
#   ./mmo/play.sh --mods bodies,hub     # runtime content packages ($PC_MODS)
#   ./mmo/play.sh --mods-dir DIR        # where those packages live ($PC_MODS_DIR;
#                                       # default: this tree's mods/, or mods/
#                                       # next to the ROM)
#   ./mmo/play.sh --print-env           # print the port's environment and exit
#   ./mmo/play.sh --pair                # two full clients side by side, one
#                                       # command: distinct channels, saves and
#                                       # sessions (admin and test). Implies
#                                       # --online: both join one server.
#   ./mmo/play.sh --frames N --dump-frames DIR   # no viewer; the port renders N
#                                                # frames to PNGs in DIR and exits
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE="${ENGINE_DIR:-$ROOT/../engine/pokeplatinum}"
FUSED="$ROOT/build/fused"
TARGET="$FUSED/pokeplatinum"
VIEWER="$ROOT/build/openmmo-view"

SAVE=""
ROM="${PC_ROM:-}"
SERVER=""
SESSION=0
USER_NAME=""
PASS=""
CHARACTER=""
GAMEPORT=""
WORLD=0
FRESH=0
PACE=1
FRAMES=""
DUMP=""
PAIR=0
WANT_WAYLAND=0
PRINT_ENV=0
MODS_LIST="${PC_MODS:-}"
MODS_DIR="${PC_MODS_DIR:-}"
VIEWER_ARGS=()

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --online)  SESSION=1; shift;;
        --server)  SERVER="$2"; SESSION=1; shift 2;;
        --user)    USER_NAME="$2"; shift 2;;
        --pass)    PASS="$2"; shift 2;;
        --character) CHARACTER="$2"; shift 2;;
        --gameport) GAMEPORT="$2"; shift 2;;
        --world)   WORLD=1; shift;;
        --save)    SAVE="$2"; shift 2;;
        --new)     FRESH=1; shift;;
        --rom)     ROM="$2"; shift 2;;
        --mods)    MODS_LIST="$2"; shift 2;;
        --mods-dir) MODS_DIR="$2"; shift 2;;
        --print-env) PRINT_ENV=1; shift;;
        --unpaced) PACE=0; shift;;
        --frames)  FRAMES="$2"; shift 2;;
        --dump-frames) DUMP="$2"; shift 2;;
        --pair)    PAIR=1; shift;;
        --wayland) WANT_WAYLAND=1; shift;;
        --software) export SDL_RENDER_DRIVER=software; shift;;
        --wide)    VIEWER_ARGS+=(--layout wide); shift;;
        --scale|--render-scale|--filter)
                   VIEWER_ARGS+=("$1" "$2"); shift 2;;
        --integer|--stretch|--fullscreen)
                   VIEWER_ARGS+=("$1"); shift;;
        -h|--help) usage; exit 0;;
        *) printf 'play: unknown option %s (try --help)\n' "$1" >&2; exit 2;;
    esac
done

[[ -x "$TARGET" ]] || { printf 'play: no %s -- run `make -C mmo fused`\n' "$TARGET" >&2; exit 1; }

if [[ -z "$ROM" ]]; then
    if [[ -f "$ENGINE/build/rom/pokeplatinum.us.nds" ]]; then
        ROM="$ENGINE/build/rom/pokeplatinum.us.nds"
    else
        printf 'play: no ROM. Pass --rom FILE or set $PC_ROM (the fused binary\n' >&2
        printf '      cannot guess it; see mmo/mods/openmmo/README.md).\n' >&2
        exit 1
    fi
fi

# Runtime content packages. The engine defaults PC_MODS_DIR to pc/mods, which
# is the wrong tree from this script. Point it at this repo's mods/ (or a
# player folder next to the ROM) so a claimed package is looked for here, and
# refuse to invent a package list: an unset PC_MODS loads nothing.
if [[ -z "$MODS_DIR" ]]; then
    if [[ -d "$ROOT/mods" ]]; then
        MODS_DIR="$ROOT/mods"
    else
        romdir=$(dirname "$ROM")
        if [[ -d "$romdir/mods" ]]; then
            MODS_DIR="$romdir/mods"
        else
            MODS_DIR="$ROOT/mods"
        fi
    fi
fi

# The port's environment. Shared by the live start and --print-env so a
# check of the front door is looking at the same array the process gets.
#
#   fill_envv CHANNEL SAVE_FILE SERVER USER PASS
fill_envv() {
    local chan="$1" save="$2" server="$3" user="$4" pass="$5"
    envv=(PC_VIEW="$chan" PC_ROM="$ROM" PC_MODS_DIR="$MODS_DIR")
    [[ -n "$MODS_LIST" ]] && envv+=(PC_MODS="$MODS_LIST")
    [[ -n "${PC_MODFS:-}" ]] && envv+=(PC_MODFS="$PC_MODFS")
    if [[ "$SESSION" -eq 1 ]]; then
        envv+=(PC_SAVE=none)
    else
        envv+=(PC_SAVE="$save")
    fi
    [[ "$PACE" -eq 0 ]] && envv+=(PC_PACE=0)
    [[ -n "$FRAMES" ]] && envv+=(PC_FRAMES="$FRAMES")
    [[ -n "$DUMP" ]]   && envv+=(PC_DUMP_FRAMES="$DUMP")
    # Whether, not where: the address is the build's (mmo/include/endpoint.h).
    # A named --server is only read by a build from this tree.
    [[ "$SESSION" -eq 1 ]] && envv+=(OPENMMO_SESSION=1)
    [[ -n "$server" ]] && envv+=(OPENMMO_SERVER="$server")
    [[ -n "$user" ]]   && envv+=(OPENMMO_USER="$user")
    [[ -n "$pass" ]]   && envv+=(OPENMMO_PASS="$pass")
    [[ -n "$CHARACTER" ]] && envv+=(OPENMMO_CHARACTER="$CHARACTER")
    [[ -n "$GAMEPORT" ]] && envv+=(OPENMMO_GAMEPORT="$GAMEPORT")
    [[ "$WORLD" -eq 1 ]] && envv+=(OPENMMO_BOOT_WORLD=1)
}

# One client, in its own process, with its own channel, save and session.
#
#   run_client CHANNEL SAVE_FILE SERVER USER PASS
run_client() {
    local chan="$1" save="$2" server="$3" user="$4" pass="$5"
    local envv pid i
    [[ "$FRESH" -eq 1 ]] && rm -f "$save"

    fill_envv "$chan" "$save" "$server" "$user" "$pass"

    if [[ -n "$DUMP" ]]; then
        mkdir -p "$DUMP"
        env "${envv[@]}" "$TARGET"
        return $?
    fi

    env "${envv[@]}" "$TARGET" &
    pid=$!
    for i in $(seq 1 100); do
        [[ -e "/dev/shm/$chan" ]] && break
        kill -0 "$pid" 2>/dev/null || { printf 'play: the port exited before publishing a frame\n' >&2; return 1; }
        sleep 0.1
    done
    [[ -e "/dev/shm/$chan" ]] || { printf 'play: the port never published a channel\n' >&2; kill "$pid" 2>/dev/null; return 1; }
    echo "$pid"
}

# Take down only what we launched, by the pid we launched it with, and unlink
# only the channels we created (named for this pid). Ports we did not start and
# other sessions' channels are never touched.
PIDS=()
CHANS=()
cleanup() {
    local p c
    for p in "${PIDS[@]:-}"; do
        [[ -n "$p" ]] && kill "$p" 2>/dev/null
    done
    # A session has three pages, not one: the frames, the characters typed into
    # them, and where the session stands. Removing only the first leaves the
    # other two behind for every run ever played.
    for c in "${CHANS[@]:-}"; do
        [[ -n "$c" ]] && rm -f "/dev/shm/$c" "/dev/shm/$c.text" "/dev/shm/$c.status"
    done
}
trap cleanup EXIT INT TERM

# Print the port environment and stop. The fused binary is not started: this
# is how a check holds the front door to the two variables the engine reads.
if [[ "$PRINT_ENV" -eq 1 ]]; then
    envv=()
    fill_envv "openmmo-print" "${SAVE:-$FUSED/live.sav}" \
        "$SERVER" "$USER_NAME" "$PASS"
    printf '%s\n' "${envv[@]}"
    exit 0
fi

# Headless: no viewer, no display, no pairing (a dump is one client's frames).
if [[ -n "$DUMP" ]]; then
    CHAN="openmmo-$$"
    SAVE="${SAVE:-$FUSED/live.sav}"
    CHANS+=("$CHAN")
    printf 'play: headless, %s frames -> %s (channel %s)\n' "${FRAMES:-all}" "$DUMP" "$CHAN"
    run_client "$CHAN" "$SAVE" "$SERVER" "$USER_NAME" "$PASS"
    exit $?
fi

# A window needs a display. Fail loudly and early rather than nowhere.
if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    printf 'play: no $DISPLAY and no $WAYLAND_DISPLAY, this shell cannot open a\n' >&2
    printf '      window. Run it from your own terminal, or pass --dump-frames.\n' >&2
    exit 1
fi
# Prefer x11 under WSLg (measured in pc/play.sh: forced wayland makes Mesa fall
# over here). SDL_VIDEODRIVER in the environment always wins; --wayland opts out.
if [[ -z "${SDL_VIDEODRIVER:-}" && "$WANT_WAYLAND" -eq 0 ]] \
   && [[ -n "${DISPLAY:-}" ]] && [[ -e /mnt/wslg || -n "${WSL_DISTRO_NAME:-}" ]]; then
    export SDL_VIDEODRIVER=x11
fi

if [[ ! -x "$VIEWER" ]]; then
    printf 'play: no viewer at %s -- run `make -C mmo viewer`\n' "$VIEWER" >&2
    exit 1
fi

if [[ "$PAIR" -eq 1 ]]; then
    # Two full clients, side by side. Distinct channels, saves and accounts, one
    # shared login server, a pair is always a session, because two clients that
    # cannot see each other is not the thing being looked at.
    # The two viewers run in the foreground; closing one ends the session
    # (cleanup kills both ports).
    SESSION=1
    CHAN_A="openmmo-a-$$"; CHAN_B="openmmo-b-$$"
    CHANS+=("$CHAN_A" "$CHAN_B")
    SAVE_A="$FUSED/live-a.sav"; SAVE_B="$FUSED/live-b.sav"
    USER_A="${USER_NAME:-admin}"; PASS_A="${PASS:-admin}"
    USER_B="test"; PASS_B="test"

    printf 'play: pair, window A (%s, %s) + window B (%s, %s)\n' \
        "$CHAN_A" "$USER_A" "$CHAN_B" "$USER_B"
    [[ -n "$SERVER" ]] && printf 'play: both on login server %s\n' "$SERVER"

    PID_A="$(run_client "$CHAN_A" "$SAVE_A" "$SERVER" "$USER_A" "$PASS_A")" || exit 1
    PIDS+=("$PID_A")
    PID_B="$(run_client "$CHAN_B" "$SAVE_B" "$SERVER" "$USER_B" "$PASS_B")" || exit 1
    PIDS+=("$PID_B")

    "$VIEWER" "$CHAN_A" ${VIEWER_ARGS[@]+"${VIEWER_ARGS[@]}"} &
    V_A=$!
    "$VIEWER" "$CHAN_B" ${VIEWER_ARGS[@]+"${VIEWER_ARGS[@]}"} &
    V_B=$!
    wait "$V_A" "$V_B"
    exit 0
fi

# One window.
CHAN="openmmo-$$"
CHANS+=("$CHAN")
SAVE="${SAVE:-$FUSED/live.sav}"
printf 'play: %s\n' "$ROM"
printf 'play: save %s%s\n' "$SAVE" "$([[ -f "$SAVE" ]] && echo '' || echo ' (new)')"
if [[ -n "$MODS_LIST" ]]; then
    printf 'play: mods %s (%s)\n' "$MODS_DIR" "$MODS_LIST"
else
    printf 'play: mods %s\n' "$MODS_DIR"
fi
if [[ "$SESSION" -eq 1 ]]; then
    printf 'play: session -> %s as %s\n' "${SERVER:-the built-in server}" \
        "${USER_NAME:-test}"
fi

PID="$(run_client "$CHAN" "$SAVE" "$SERVER" "$USER_NAME" "$PASS")" || exit 1
PIDS+=("$PID")
"$VIEWER" "$CHAN" ${VIEWER_ARGS[@]+"${VIEWER_ARGS[@]}"}
