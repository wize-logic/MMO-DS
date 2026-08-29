#!/usr/bin/env bash
#
# Empty both databases and build the schemas again, leaving this checkout the
# way a fresh install starts: no accounts, no characters, nothing on the shelf.
#
#   ./reset-db.sh                     # asks first
#   ./reset-db.sh --yes               # does not ask
#
# The first account made afterwards is a developer, so the way back is
# ./start-server.sh then server.login create-user <name> <password>.
#
# Both databases go together on purpose: a user id is the only thing tying them
# to each other, so emptying one leaves the other holding characters owned by
# accounts that no longer exist. For one side only, each server takes
# `reset-db --yes` on its own.
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

[[ -f .env ]] || { printf '\033[31m[openmmo]\033[0m %s\n' ".env not found. It holds the local database settings." >&2; exit 1; }

# Read up front, because the connection check below and the servers themselves
# both take their database settings from here.
set -a
# shellcheck disable=SC1091
. ./.env
set +a

LOGIN_PORT="${OPENMMO_LOGIN_PORT:-2106}"
LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port="$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)"
GAME_PORT="${OPENMMO_GAMEPORT:-${cfg_port:-7777}}"

JAVA_HOME="${JAVA_HOME:-$HOME/.jdks/jdk-25.0.4+7}"
ASSUME_YES=0
[[ "${1:-}" == "--yes" ]] && ASSUME_YES=1

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }
die()  { printf '\033[31m[openmmo]\033[0m %s\n' "$1" >&2; exit 1; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

[[ -x "$JAVA_HOME/bin/java" ]] || die "No JDK at $JAVA_HOME. Set JAVA_HOME to a JDK 25."
command -v docker &>/dev/null || die "docker not found; the databases run in containers."
export JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"

# A live server can quietly undo this: the game server holds characters in
# memory and writes them back on a timer, so a reset with one still up comes
# back half full a minute later.
#
# Ports are the friendly half of the check, not the whole of it. The game
# server takes its port from -Dserver.port, so one on 7778 for a two-window
# test is in no fixed list, and an unwritten launcher config leaves this
# guessing at 7777.
for port_pair in "login:$LOGIN_PORT" "game:$GAME_PORT" "game:7777" "game:7778"; do
    name="${port_pair%%:*}"
    port="${port_pair##*:}"
    if listening "$port"; then
        die "a $name server is still up on $port. Run ./stop-server.sh first."
    fi
done

if [[ $ASSUME_YES -eq 0 ]]; then
    warn "This deletes every account, character, guild and listing in this checkout."
    read -r -p "Type 'reset' to go ahead: " answer
    [[ "$answer" == "reset" ]] || die "nothing was touched"
fi

# They have to be up to be emptied, and they are the slow part to start.
say "databases"
docker compose up -d login-db game-db >/dev/null || die "the database containers would not start"

# The check that settles it: ask each database who is connected. That sees a
# server on any port, on any host, started by anybody.
connections() {
    local container="$1" user="$2" db="$3" pass="$4"
    docker exec -e PGPASSWORD="$pass" "$container" \
        psql -U "$user" -d "$db" -tAc \
        "SELECT count(*) FROM pg_stat_activity
          WHERE datname = current_database() AND pid <> pg_backend_pid()" 2>/dev/null \
        | tr -d '[:space:]'
}

for db_spec in \
    "login-db:${LOGIN_DB_USER:-openmmo_login_user}:${LOGIN_DB_NAME:-openmmo_login_db}:${LOGIN_DB_PASSWORD:-}" \
    "game-db:${GAME_DB_USER:-openmmo_game_user}:${GAME_DB_NAME:-openmmo_game_db}:${GAME_DB_PASSWORD:-}"; do
    IFS=: read -r container user db pass <<<"$db_spec"
    open="$(connections "$container" "$user" "$db" "$pass")"
    # An unreadable answer is not a zero. Say so rather than pressing on.
    [[ "$open" =~ ^[0-9]+$ ]] || die "could not ask $container who is connected to it"
    if [[ "$open" -gt 0 ]]; then
        die "$open connection(s) still open to '$db'. Stop whatever is holding them first."
    fi
done

say "building the server distributions"
./gradlew :server.login:installDist :server.game:installDist </dev/null >/dev/null \
    || die "the servers would not build"

say "emptying the login database"
./server.login/build/install/server.login/bin/server.login reset-db --yes

say "emptying the game database"
./server.game/build/install/server.game/bin/server.game reset-db --yes

say "clean. ./start-server.sh, then create-user, and that account is a developer."
