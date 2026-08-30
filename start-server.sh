#!/usr/bin/env bash
# Start the databases, the login server and the game server for local play.
# Safe to run twice: anything already listening is left alone. The website counts as
# one of the servers: it answers registration and the online count.
#
#   ./start-server.sh                 # start everything that is not up
#   OPENMMO_GAMEPORT=7777 ./start-server.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"

# Default the game port to whatever the launcher is set to dial, so that
# start-server and play agree without being told twice. Falls back to the
# server's own default when there is no launcher config yet.
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port="$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)"
GAME_PORT="${OPENMMO_GAMEPORT:-${cfg_port:-7777}}"

JAVA_HOME="${JAVA_HOME:-$HOME/.jdks/jdk-25.0.4+7}"
LOG_DIR="${OPENMMO_LOG_DIR:-/tmp}"
LOGIN_LOG="$LOG_DIR/openmmo-login-server.log"
GAME_LOG="$LOG_DIR/openmmo-game-server.log"
WAIT_SECS="${OPENMMO_WAIT_SECS:-120}"

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

[[ -f .env ]] || die ".env not found. It holds the local database and key settings."
[[ -x "$JAVA_HOME/bin/java" ]] || die "No JDK at $JAVA_HOME. Set JAVA_HOME to a JDK 25."
command -v docker &>/dev/null || die "docker not found; the databases run in containers."
export JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"

java_ver="$("$JAVA_HOME/bin/java" -version 2>&1 | head -1)"
case "$java_ver" in
    *'"25'*) ;;
    *) warn "JAVA_HOME looks like $java_ver, not a JDK 25. Carrying on." ;;
esac

say "databases"
docker compose up -d login-db game-db >/dev/null || die "the database containers would not start"

if listening "$LOGIN_PORT"; then
    say "login server already listening on $LOGIN_PORT"
else
    say "starting login server on $LOGIN_PORT (log: $LOGIN_LOG)"
    ./gradlew :server.login:run </dev/null >"$LOGIN_LOG" 2>&1 &
fi

# The game server runs from its installed distribution rather than the gradle
# run task, because that is the only one of the two that takes a port: the
# port lives in application.conf with no environment override, and the run
# task forwards a fixed list of variables that does not include it.
if listening "$GAME_PORT"; then
    say "game server already listening on $GAME_PORT"
else
    say "building the game server distribution"
    ./gradlew :server.game:installDist </dev/null >"$GAME_LOG" 2>&1 \
        || { tail -20 "$GAME_LOG"; die "the game server would not build"; }

    # The database settings and the session secret come from .env; the signing
    # key is left blank there, so point at the one in the tree.
    set -a
    # shellcheck disable=SC1091
    . ./.env
    set +a
    : "${OPENMMO_GAME_PRIVATE_KEY_FILE:=$PWD/server.game/src/main/resources/game.private.pem}"
    export OPENMMO_GAME_PRIVATE_KEY_FILE

    say "starting game server on $GAME_PORT (log: $GAME_LOG)"
    JAVA_OPTS="-Dserver.port=$GAME_PORT" \
        ./server.game/build/install/server.game/bin/server.game \
        </dev/null >>"$GAME_LOG" 2>&1 &
fi

# The website answers registration and the online count, so a server pair without it
# has no way to make an account. A systemd unit when installed, the built distribution
# in a checkout, and skipped either way if the port is taken.
WEB_PORT="${OPENMMO_WEB_PORT:-8088}"
WEB_LOG="$LOG_DIR/openmmo-web-server.log"
start_website() {
    if listening "$WEB_PORT"; then
        say "website already listening on $WEB_PORT"
        return 0
    fi
    if systemctl list-unit-files openmmo-web.service &>/dev/null &&
       systemctl cat openmmo-web.service &>/dev/null; then
        say "starting the website (systemd: openmmo-web)"
        sudo -n systemctl start openmmo-web 2>/dev/null ||
            warn "could not start openmmo-web; run: sudo systemctl start openmmo-web"
        return 0
    fi
    if [[ -x ./server.web/build/install/server.web/bin/server.web ]]; then
        say "starting the website on $WEB_PORT (log: $WEB_LOG)"
        OPENMMO_WEB_PORT="$WEB_PORT" OPENMMO_WEB_ROOT="${OPENMMO_WEB_ROOT:-$PWD/web/public}" \
            ./server.web/build/install/server.web/bin/server.web \
            </dev/null >"$WEB_LOG" 2>&1 &
    else
        warn "no website: build it with ./gradlew :server.web:installDist, or install it with sudo ./web/deploy.sh"
    fi
}
start_website

say "waiting for both to accept connections"
deadline=$((SECONDS + WAIT_SECS))
while (( SECONDS < deadline )); do
    if listening "$LOGIN_PORT" && listening "$GAME_PORT"; then
        say "login on $LOGIN_PORT, game on $GAME_PORT, ready"
        listening "$WEB_PORT" && say "website on $WEB_PORT" ||
            warn "the website is not on $WEB_PORT; registration and the online count are down"
        say "play with ./play.sh, stop with ./stop-server.sh"
        exit 0
    fi
    sleep 2
done

warn "still not up after ${WAIT_SECS}s. Last lines of each log:"
for l in "$LOGIN_LOG" "$GAME_LOG"; do
    printf '\n--- %s\n' "$l"
    tail -15 "$l" 2>/dev/null || echo "(no log)"
done
exit 1
