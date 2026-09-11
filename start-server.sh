#!/usr/bin/env bash
# Start the databases, the login server and the game server for local play.
# Safe to run twice: anything already listening is left alone.
#
#   ./start-server.sh                 # start everything that is not up
#   ./start-server.sh --dry-run       # print what it would do, start nothing
#   OPENMMO_GAMEPORT=7777 ./start-server.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

# --dry-run makes every decision this script makes, the two ports, whether
# systemd owns the servers, whether the secret this repository ships with is
# allowed, prints them, and starts nothing. It wants no database, no JDK and
# no free port, which is what makes those decisions checkable.
DRY_RUN=0
case "${1:-}" in
    "")        ;;
    --dry-run) DRY_RUN=1 ;;
    *)         die "unknown argument: $1 (the only one is --dry-run)" ;;
esac

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"

# LISTEN ON IPv4 AS WELL AS IPv6, or a Windows client cannot reach this at
# all.
IPV4_STACK="-Djava.net.preferIPv4Stack=true"

JAVA_HOME="${JAVA_HOME:-$HOME/.jdks/jdk-25.0.4+7}"
LOG_DIR="${OPENMMO_LOG_DIR:-/tmp}"
LOGIN_LOG="$LOG_DIR/openmmo-login-server.log"
GAME_LOG="$LOG_DIR/openmmo-game-server.log"
WEB_PORT="${OPENMMO_WEB_PORT:-8088}"
WEB_LOG="$LOG_DIR/openmmo-web-server.log"
WAIT_SECS="${OPENMMO_WAIT_SECS:-120}"

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

# A machine that has had deploy/install.sh run on it keeps the servers under
# systemd, where they restart on their own and come back after a reboot. Two
# ways to start the same servers is one way too many, the second one binds
# nothing and reads like the first has failed, so hand over rather than
# racing it for the ports.
if systemctl cat openmmo.target &>/dev/null && [[ "${OPENMMO_IGNORE_SYSTEMD:-0}" != 1 ]]; then
    say "this machine runs OpenMMO under systemd"
    if (( DRY_RUN )); then
        say "would start openmmo.target (databases, login, game, website)"
        say "nothing started (--dry-run)"
        exit 0
    fi
    say "starting openmmo.target (databases, login, game, website)"
    sudo systemctl start openmmo.target
    systemctl --no-pager --lines=0 status 'openmmo-*' | grep -E '^(●|×|\s+Active)' || true
    say "stop with ./stop-server.sh, or sudo systemctl stop openmmo.target"
    exit 0
fi

[[ -f .env ]] || die ".env not found. It holds the local database and key settings."

# Every decision below is made out of .env, so read it before making one. This
# used to be sourced much further down and for the game server alone, which left
# the secret test below it asking a shell that never has the secret: the answer
# was "nothing configured" on every machine, so the refusal that guards a public
# signing key was waived even where a real secret was set.
set -a
# shellcheck disable=SC1091
. ./.env
set +a

# Where the GAME server listens is not a local preference.
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port="$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)"
GAME_PORT="${OPENMMO_GAMEPORT:-${GAME_SERVER_PORT:-${cfg_port:-7777}}}"

# Both servers refuse to start on the session secret this repository ships
# with, because a public secret is a signing key anybody can use to mint a
# join ticket for any account.
if [[ -z "${OPENMMO_SESSION_SECRET:-}" ]]; then
    if [[ -n "${OPENMMO_SESSION_SECRET+set}" ]]; then
        die "OPENMMO_SESSION_SECRET is set to nothing in .env, which both servers read as a \
secret of length zero and refuse. Run ./setup.sh to generate one, or delete the line to play \
on the secret this repository ships with."
    fi
    export OPENMMO_ALLOW_DEV_SECRET=1
fi

if (( DRY_RUN )); then
    say "login on $LOGIN_PORT, game on $GAME_PORT, website on $WEB_PORT"
    if [[ -n "${OPENMMO_SESSION_SECRET:-}" ]]; then
        say "session secret: configured, so the shipped one is refused"
    else
        say "session secret: unset, so the shipped one is allowed"
    fi
    say "nothing started (--dry-run)"
    exit 0
fi

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
    # Whatever this server needs out of .env is named in server.login/build.gradle.kts,
    # because `run` reads .env there and .env is never in this shell's environment.
    # OPENMMO_ALLOW_DEV_SECRET is on that list for the decision made above.
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

    # The database settings and the session secret came from .env at the top;
    # the signing key is left blank there, so point at the one in the tree.
    : "${OPENMMO_GAME_PRIVATE_KEY_FILE:=$PWD/server.game/src/main/resources/game.private.pem}"
    export OPENMMO_GAME_PRIVATE_KEY_FILE

    say "starting game server on $GAME_PORT (log: $GAME_LOG)"
    JAVA_OPTS="-Dserver.port=$GAME_PORT $IPV4_STACK" \
        ./server.game/build/install/server.game/bin/server.game \
        </dev/null >>"$GAME_LOG" 2>&1 &
fi

# The website. It is the one process that answers /api/register and /api/status,
# so a server pair without it has no way to make an account and no online count.
# Installed, it is a systemd service; in a checkout it is the installed
# distribution run in the foreground. Either way, skip it if the port is taken.
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
