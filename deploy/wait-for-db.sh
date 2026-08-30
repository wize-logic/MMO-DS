#!/bin/sh
# Wait until both database containers are accepting connections.
#
# `docker start` returns when the container is running, a good while before postgres
# is listening, and a server that finds no database on boot just exits and retries.
# This turns that race into an ordered start.
set -eu

LOGIN_PORT="${LOGIN_DB_PORT:-20011}"
GAME_PORT="${GAME_DB_PORT:-20021}"
DEADLINE=$(( $(date +%s) + ${OPENMMO_DB_WAIT_SECS:-120} ))

ready() {
    docker exec "$1" pg_isready -q -U postgres >/dev/null 2>&1 ||
        docker exec "$1" pg_isready -q >/dev/null 2>&1
}

while :; do
    if ready login-db && ready game-db; then
        echo "login-db and game-db are accepting connections"
        exit 0
    fi
    [ "$(date +%s)" -lt "$DEADLINE" ] || break
    sleep 2
done

echo "databases were not ready in time (login-db :$LOGIN_PORT, game-db :$GAME_PORT)" >&2
docker ps --filter name=login-db --filter name=game-db --format '{{.Names}} {{.Status}}' >&2
exit 1
