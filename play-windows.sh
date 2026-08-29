#!/usr/bin/env bash
# Open the WINDOWS build of the game, from this WSL shell. Builds whatever is
# missing, stages it where Windows can read it quickly, points it at the
# servers running in here, and presses Play.
#
#   ./play-windows.sh                 # straight into the game
#   ./play-windows.sh --menu          # open the front door instead
#   ./play-windows.sh --plan          # print the two command lines Play would
#                                     # run, and stop
#   ./play-windows.sh --user U        # log in as U (default: whatever the
#                                     # Linux launcher.cfg last used)
#   ./play-windows.sh --server H[:P]  # dial somewhere else (default: the
#                                     # address Windows reaches this WSL on)
#   ./play-windows.sh --gameport N    # override the game port
#   ./play-windows.sh --no-build      # skip the build check and just run
#   ./play-windows.sh --fresh-settings  # seed the Windows settings from the
#                                     # Linux ones again, discarding whatever
#                                     # the front door has saved since
#   ./play-windows.sh --stage-only    # stage, print the folder, do not run
#   the address    From Windows, 127.0.0.1 is the windows machine. The servers
#                  listen in here, so the client has to dial the address WSL
#                  is reachable on, measured on this host: 127.0.0.1:2106 is
#                  refused and eth0's 2106 is open. A build from this tree has
#                  SERVER_SETTABLE=1, so OPENMMO_SERVER replaces the compiled
#                  pin; a release build does not, and this script says so
#                  rather than dialling into nothing.
#   the boundary   A Windows process reading this repo reads it over
#                  \\wsl.localhost, and the game reads its cartridge inside
#                  the frame that asks for it: 20-57 ms a read from there
#                  against 0.08 ms from NTFS. The exes and the ROM are staged
#                  on the Windows TEMP, which is the difference between three
#                  frames in a hundred missing the deadline and almost none.
#   the settings   The Windows front door reads %APPDATA%\openmmo, never
#                  ~/.config/openmmo, so nothing set up on this side reaches
#                  it. APPDATA is pointed at a folder of this script's own and
#                  the settings are seeded there from the Linux ones,
#                  translated into Windows spellings, which also keeps a dev
#                  run out of whatever real install is on the desktop.
#   appdata/       the settings and the sign-in token. Not under the build
#                  stamp, because a rebuild must not throw away the password
#                  you typed. Seeded once and then the front door's to keep;
#                  --fresh-settings seeds it again.
#   <stamp>/       the exes, the pictures, the cartridge and the logs, keyed
#                  by the newest binary's timestamp. A rebuild gets a folder
#                  of its own, which is also how Windows is stopped from
#                  serving the stale image it caches for a rewritten path.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ROOT="$(pwd)"
export ENGINE_DIR="${ENGINE_DIR:-$ROOT/engine/pokeplatinum}"

MODE=play
BUILD=1
STAGE_ONLY=0
FRESH_CFG=0
WANT_USER=""
WANT_SERVER="${OPENMMO_SERVER:-}"
WANT_GAMEPORT="${OPENMMO_GAMEPORT:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --menu)           MODE=menu; shift;;
        --plan)           MODE=plan; shift;;
        --user)           WANT_USER="$2"; shift 2;;
        --server)         WANT_SERVER="$2"; shift 2;;
        --gameport)       WANT_GAMEPORT="$2"; shift 2;;
        --no-build)       BUILD=0; shift;;
        --fresh-settings) FRESH_CFG=1; shift;;
        --stage-only)     STAGE_ONLY=1; shift;;
        -h|--help)    awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; exit 0;;
        *) printf 'play-windows: unknown option %s (try --help)\n' "$1" >&2; exit 2;;
    esac
done

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

# ------------------------------------------------------------------ the host
#
# Every failure below is "you are not on WSL, or interop is off", and each one
# is worth its own sentence: the second is a setting a person can turn back on
# (interop.enabled in /etc/wsl.conf) and the first is not.
[[ -d /mnt/c ]] || die "no /mnt/c, this script runs Windows binaries and needs a WSL shell with drive mounts"
command -v wslpath >/dev/null 2>&1 || die "no wslpath, this does not look like WSL"
[[ -x /mnt/c/Windows/System32/cmd.exe ]] || die "cannot see cmd.exe; is WSL interop enabled?"

WINBUILD="$ROOT/mmo/build/win"
LAUNCHER="$WINBUILD/openmmo-launch.exe"
VIEWER="$WINBUILD/openmmo-view.exe"
GAME="$WINBUILD/fused/pokeplatinum.exe"
DLL="$WINBUILD/SDL2.dll"
MK=(make -C "$ROOT/mmo" -f Makefile.win)

# ----------------------------------------------------------------- the build
if [[ "$BUILD" -eq 1 ]]; then
    if [[ ! -f "$WINBUILD/deps/.ok" ]]; then
        say "cross-building SDL2, raylib and freetype (once per tree; this needs the network)"
        "${MK[@]}" winlibs || die "winlibs failed, see mmo/tools/win_deps.sh"
    fi
    [[ -d "$ENGINE_DIR" ]] || die "ENGINE_DIR does not exist: $ENGINE_DIR"
    say "checking the Windows build is current"
    "${MK[@]}" programs fused -j"$(nproc)" || die "the Windows build failed"
fi

for f in "$LAUNCHER" "$VIEWER" "$GAME" "$DLL"; do
    [[ -e "$f" ]] || die "missing $(basename "$f") -- run without --no-build, or: make -C mmo -f Makefile.win winlibs programs fused"
done

# A release build compiles the override out (SERVER_SETTABLE=0), so nothing
# here could point it anywhere. Say that before staging 90 MB rather than
# after the login connect is refused. The pin is a plain string in the exe.
if ! grep -aq 'OPENMMO_SERVER' "$GAME" 2>/dev/null; then
    warn "this game exe has no OPENMMO_SERVER seam (a release build?), it will dial its compiled-in pin whatever this script says"
fi

# --------------------------------------------------------------- the address
#
# The address Windows reaches this WSL on. Not 127.0.0.1: over there that is
# the Windows machine, and the servers are in here.
if [[ -z "$WANT_SERVER" ]]; then
    wsl_ip="$(ip -4 -o addr show eth0 2>/dev/null | awk '{ sub(/\/.*/, "", $4); print $4; exit }')"
    if [[ -z "$wsl_ip" ]]; then
        warn "no eth0 address, falling back to 127.0.0.1, which only works with mirrored networking"
        wsl_ip=127.0.0.1
    fi
    WANT_SERVER="$wsl_ip"
fi

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"
case "$WANT_SERVER" in *:*) LOGIN_PORT="${WANT_SERVER##*:}";; esac

# The game port is not in the settings file any more (the key was retired), so
# rather than guess 7777 at a tree where start-server.sh was told 7778, take
# the one that is actually listening. An explicit --gameport still wins.
if [[ -z "$WANT_GAMEPORT" ]]; then
    for p in 7777 7778; do
        if listening "$p"; then WANT_GAMEPORT="$p"; break; fi
    done
    WANT_GAMEPORT="${WANT_GAMEPORT:-7777}"
fi

listening "$LOGIN_PORT" || warn "nothing is listening on $LOGIN_PORT here. Start one with ./start-server.sh"
listening "$WANT_GAMEPORT" || warn "nothing is listening on $WANT_GAMEPORT here"

# --------------------------------------------------------------- the staging
wintmp="$(cd /mnt/c 2>/dev/null && /mnt/c/Windows/System32/cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r\n')"
wintmp="$(wslpath -u "$wintmp" 2>/dev/null || true)"
[[ -d "${wintmp:-}" ]] || wintmp=/mnt/c/Windows/Temp
[[ -d "$wintmp" ]] || die "no writable Windows temp directory (tried %TEMP% and C:\\Windows\\Temp)"

STAGE_ROOT="$wintmp/openmmo-wsl"
stamp="$(date -d "@$(stat -c %Y "$LAUNCHER" "$VIEWER" "$GAME" "$DLL" | sort -n | tail -1)" +%Y%m%d-%H%M%S)"
STAGE="$STAGE_ROOT/$stamp"
# appdata is a sibling of the stamped folders, not a child of one: the sign-in
# token is what a rebuild must not take away.
APPDIR="$STAGE_ROOT/appdata"
mkdir -p "$STAGE/fused" "$STAGE/res/launcher" "$STAGE/rom" "$STAGE/logs" \
         "$APPDIR/openmmo" \
    || die "cannot write into $wintmp"

stage_file() {  # src dst -- copy only when it differs, so a relaunch is free
    [[ -f "$2" && ! "$1" -nt "$2" ]] && return 0
    cp -f "$1" "$2" || die "could not stage $(basename "$1") into $STAGE"
}

stage_file "$LAUNCHER" "$STAGE/openmmo-launch.exe"
stage_file "$VIEWER"   "$STAGE/openmmo-view.exe"
stage_file "$DLL"      "$STAGE/SDL2.dll"
stage_file "$GAME"     "$STAGE/fused/pokeplatinum.exe"
# The front door draws these two and finds them at res/launcher/ beside
# itself; without them it opens with a blank background and no wordmark.
stage_file "$ROOT/mmo/res/launcher/bg.png"       "$STAGE/res/launcher/bg.png"
stage_file "$ROOT/mmo/res/launcher/wordmark.png" "$STAGE/res/launcher/wordmark.png"

STAGE_WIN="$(wslpath -w "$STAGE")" || die "cannot name $STAGE in Windows spelling"
APPDIR_WIN="$(wslpath -w "$APPDIR")" || die "cannot name $APPDIR in Windows spelling"

# Keep the three newest builds and no more: each is about 240 MB once it has a
# cartridge in it. `appdata` is skipped before the count rather than trusted to
# sort order, it is not a build, and deleting the sign-in token to save disk
# is not a trade this should make.
kept=0
while IFS= read -r old; do
    [[ "$old" == "$APPDIR" ]] && continue
    kept=$((kept + 1))
    (( kept <= 3 )) && continue
    [[ "$old" == "$STAGE" ]] && continue
    rm -rf "$old"
done < <(ls -dt "$STAGE_ROOT"/*/ 2>/dev/null | sed 's:/$::')

# ------------------------------------------------------------------- the ROM
#
# 134 MB, and the one file the game reads during a frame, which is the whole
# reason for staging. Copied only when it differs.
lin_cfg="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_get() { awk -v k="$1" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }' "$lin_cfg" 2>/dev/null; }

ROM="${PC_ROM:-$(cfg_get rom)}"
ROM="${ROM:-$ENGINE_DIR/build/rom/pokeplatinum.us.nds}"
[[ -f "$ROM" ]] || die "no cartridge image at $ROM (set PC_ROM, or put one in the launcher's settings)"
if [[ ! -f "$STAGE/rom/$(basename "$ROM")" || "$ROM" -nt "$STAGE/rom/$(basename "$ROM")" ]]; then
    say "staging the cartridge on $STAGE_WIN (once per build)"
    cp -f "$ROM" "$STAGE/rom/$(basename "$ROM")" || die "could not stage the cartridge"
fi
ROM_WIN="$STAGE_WIN\\rom\\$(basename "$ROM")"

# --------------------------------------------------------------- the settings
CFG="$APPDIR/openmmo/launcher.cfg"
if [[ "$FRESH_CFG" -eq 1 || ! -f "$CFG" ]]; then
    {
        printf '# seeded by play-windows.sh; the front door rewrites it as you use it\n'
        user="${WANT_USER:-$(cfg_get user)}"
        [[ -n "$user" ]] && printf 'user %s\n' "$user"
        printf 'rom %s\n' "$ROM_WIN"
        # The rest of the picture, verbatim: these are numbers and words, not
        # paths, so they cross unchanged.
        for k in scale viewport render-scale hd3d layout filter fit pace \
                 button-mode fullscreen audio discord music sfx soundtrack \
                 soundfont mods; do
            v="$(cfg_get "$k")"
            [[ -n "$v" ]] && printf '%s %s\n' "$k" "$v"
        done
        # ...and the ones that are paths. They stay in the tree and reach
        # Windows as UNC names: each is read once at startup, so the share
        # costs nothing a frame can feel, and copying a content tree of
        # unknown size would.
        for k in mods-dir theme rom-hg rom-bw; do
            v="$(cfg_get "$k")"
            [[ -z "$v" ]] && continue
            w="$(wslpath -w "$v" 2>/dev/null)" || continue
            printf '%s %s\n' "$k" "$w"
        done
    } > "$CFG" || die "could not write $CFG"
    say "seeded the Windows settings from $lin_cfg"
else
    # The cartridge moved with the build stamp, and it is the one row in there
    # that this script owns rather than the player: everything else survives.
    #
    # Through the environment and not `awk -v`, which reads its assignment as a
    # string literal: every backslash in a Windows path is an escape sequence
    # there, and C:\Users\<you> came out as C:UserscolbAppData with a warning
    # per lost letter. ENVIRON is taken verbatim.
    tmpcfg="$CFG.new"
    ROM_WIN="$ROM_WIN" awk '
        $1 == "rom" { print "rom " ENVIRON["ROM_WIN"]; seen = 1; next }
        { print }
        END { if (!seen) print "rom " ENVIRON["ROM_WIN"] }
    ' "$CFG" > "$tmpcfg" && mv -f "$tmpcfg" "$CFG" \
        || die "could not point the settings at the staged cartridge"
fi
[[ -n "$WANT_USER" ]] && sed -i "s|^user .*|user $WANT_USER|" "$CFG"

if [[ "$STAGE_ONLY" -eq 1 ]]; then
    say "staged, not started"
    printf '%s\n' "$STAGE_WIN"
    exit 0
fi

# Nothing to sign in with yet. The front door is where a password is typed and
# the token it comes back with is what every later Play uses, so a first run
# that went straight to Play would only reach "no password was given". Open the
# door instead and say why, rather than reporting the server's refusal for a
# question nobody was asked.
if [[ "$MODE" == play && ! -f "$APPDIR/openmmo/token" ]]; then
    warn "no saved sign-in yet, opening the front door so you can type the password once"
    MODE=menu
fi

# ------------------------------------------------------------------- the run
export OPENMMO_SERVER="$WANT_SERVER"
export OPENMMO_GAMEPORT="$WANT_GAMEPORT"
export APPDATA="$APPDIR_WIN"
export WSLENV="OPENMMO_SERVER:OPENMMO_GAMEPORT:APPDATA/w${WSLENV:+:$WSLENV}"
CFG_WIN="$APPDIR_WIN\\openmmo\\launcher.cfg"

say "server $WANT_SERVER (login $LOGIN_PORT, game $WANT_GAMEPORT)"
say "staged at $STAGE_WIN"
say "settings and sign-in in $APPDIR"
say "logs will be in $STAGE/logs"

# The working directory has to be a Windows path: a Windows process started
# from a /home/... cwd lands somewhere it did not choose, and the front door
# resolves the game and the window beside its own exe from there.
cd "$STAGE" || die "cannot enter $STAGE"

case "$MODE" in
    plan) say "what Play would run"; exec ./openmmo-launch.exe --config "$CFG_WIN" --print-plan ;;
    menu) say "opening the front door"; exec ./openmmo-launch.exe --config "$CFG_WIN" ;;
    *)    say "playing"; exec ./openmmo-launch.exe --config "$CFG_WIN" --play ;;
esac
