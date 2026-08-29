#!/usr/bin/env bash
# Stop the login and game servers. The databases keep running unless asked,
# because starting them again is the slow part and they hold the accounts.
#
#   ./stop-server.sh                  # stop the two servers
#   ./stop-server.sh --db             # stop the database containers as well
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

STOP_DB=0
[[ "${1:-}" == "--db" ]] && STOP_DB=1

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port="$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)"
GAME_PORT="${OPENMMO_GAMEPORT:-${cfg_port:-7777}}"

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

# The gradle wrapper, then the server it launched. Matched on the exact task
# and main class so a compiling gradle daemon, and anything else on this
# machine, are left alone.
kill_servers() {
    local sig="$1" pat pid comm hit=0
    for pat in ':server.login:run' \
               ':server.game:run' \
               'de.fiereu.openmmo.server.login.MainKt' \
               'de.fiereu.openmmo.server.game.MainKt'; do
        for pid in $(pgrep -f "$pat" 2>/dev/null); do
            comm=$(cat "/proc/$pid/comm" 2>/dev/null) || continue
            [[ "$comm" == java ]] || continue
            kill "-$sig" "$pid" 2>/dev/null && hit=1
        done
    done
    return $(( ! hit ))
}

say "stopping the servers"
kill_servers TERM || true

for _ in $(seq 1 15); do
    listening "$LOGIN_PORT" || listening "$GAME_PORT" || break
    sleep 1
done

still=""
listening "$LOGIN_PORT" && still="$still $LOGIN_PORT"
listening "$GAME_PORT"  && still="$still $GAME_PORT"

if [[ -n "$still" ]]; then
    warn "still listening on$still, sending KILL"
    kill_servers KILL || true
    sleep 2
fi

if listening "$LOGIN_PORT" || listening "$GAME_PORT"; then
    warn "something is still holding a port:"
    ss -ltnp 2>/dev/null | grep -E ":($LOGIN_PORT|$GAME_PORT) " || true
else
    say "both servers are down"
fi

if [[ "$STOP_DB" -eq 1 ]]; then
    say "stopping the databases"
    docker compose stop login-db game-db >/dev/null || warn "the containers would not stop"
fi
