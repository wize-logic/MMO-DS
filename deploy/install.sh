#!/usr/bin/env bash
# Install every OpenMMO service under systemd, so the whole product starts and stops
# with one command and comes back after a reboot.
#
#   sudo systemctl start openmmo.target      # databases, login, game, website
#   sudo systemctl stop  openmmo.target
#   systemctl status 'openmmo-*'
#   journalctl -u openmmo-game -f
#
#   sudo ./deploy/install.sh                 # build, install, enable
#   sudo ./deploy/install.sh --no-build      # install what is already built
#   sudo ./deploy/install.sh --start         # ...and start the target afterwards
#
# Idempotent. Existing /etc/openmmo/*.env files are kept, never overwritten:
# edit those in place, they are the configuration of the installed servers, and
# the repository .env is only ever the template they were first written from.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build=1
start=0
for arg in "$@"; do
  case "$arg" in
    --no-build) build=0 ;;
    --start) start=1 ;;
    -h | --help)
      sed -n '2,/^set -euo/{/^set -euo/d; s/^# \{0,1\}//; p;}' "$0"
      exit 0
      ;;
    *)
      echo "install: unknown option $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

[ "$(id -u)" -eq 0 ] || {
  echo "run this with sudo" >&2
  exit 1
}
[ -f "$repo/.env" ] || {
  echo "$repo/.env not found; it is the template for the installed configuration" >&2
  exit 1
}

say() { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }

listening() { ss -ltn 2>/dev/null | grep -q ":$1 "; }

# Which pid holds a port, and whether systemd started it. Cutting a hand-started
# server over to a unit is the one step this cannot do on its own: killing a live
# server drops the players on it.
holder_is_systemd() {
  local pid
  pid=$(ss -ltnp 2>/dev/null | sed -n "s/.*:$1 .*pid=\([0-9]*\).*/\1/p" | head -1)
  [ -n "$pid" ] || return 1
  grep -q '0::/system.slice' "/proc/$pid/cgroup" 2>/dev/null
}

if [ "$build" -eq 1 ]; then
  say "building the three servers"
  warn "a gradle build overwrites the classes a ./gradlew :run server is executing; stop one first"
  sudo -u "${SUDO_USER:-$USER}" "$repo/gradlew" -p "$repo" --console=plain </dev/null \
    :server.login:installDist :server.game:installDist :server.web:installDist
fi
for module in login game web; do
  [ -x "$repo/server.$module/build/install/server.$module/bin/server.$module" ] || {
    echo "server.$module is not built; run without --no-build" >&2
    exit 1
  }
done

say "service account and directories"
id -u openmmo >/dev/null 2>&1 || adduser --system --group --no-create-home openmmo
install -d -o root -g root -m 0755 /opt/openmmo /opt/openmmo/bin
# Group openmmo on the directory. Nothing in it is group readable, the env files stay
# root:root, but a process cannot open a file it cannot walk to and the signing key
# lives here.
install -d -o root -g openmmo -m 0750 /etc/openmmo

say "programs -> /opt/openmmo"
for module in login game; do
  rm -rf "/opt/openmmo/$module"
  cp -a "$repo/server.$module/build/install/server.$module" "/opt/openmmo/$module"
  chown -R root:root "/opt/openmmo/$module"
done
install -o root -g root -m 0755 "$here/wait-for-db.sh" /opt/openmmo/bin/wait-for-db

# Both servers sign with the same key and a client pins the public half, so the
# installed copy has to be the exact bytes already in use. Anything else and every
# existing client refuses the ServerHello.
say "signing key -> /etc/openmmo/game.private.pem"
install -o root -g openmmo -m 0640 \
  "$repo/server.game/src/main/resources/game.private.pem" /etc/openmmo/game.private.pem

# The environment files. Only the variables each server reads, taken from .env, with
# empty assignments dropped: an empty value is not an unset one.
java_home="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
env_from() {
  # Drop assignments with nothing on the right. The test anchors the whole line:
  # `grep -v '=$'` would also throw away a base64 secret, which ends in the padding
  # character, leaving the server on the secret this repository ships with.
  grep -E "^($1)=" "$repo/.env" | sed -E '/^[A-Za-z_][A-Za-z0-9_]*=[[:space:]]*$/d' || true
}
write_env() {
  local file="$1" pattern="$2"
  if [ -f "$file" ]; then
    say "keeping the existing $file"
    return 0
  fi
  say "$file from the repository .env"
  {
    echo "# OpenMMO. Written by deploy/install.sh, edit in place afterwards."
    echo "JAVA_HOME=$java_home"
    env_from "$pattern"
    echo "OPENMMO_GAME_PRIVATE_KEY_FILE=/etc/openmmo/game.private.pem"
  } >"$file"
  chown root:root "$file"
  chmod 0640 "$file"
}
write_env /etc/openmmo/login.env \
  'LOGIN_DB_[A-Z_]+|LOGIN_HOST|LOGIN_PORT|OPENMMO_SESSION_SECRET|OPENMMO_REMEMBER_ME_MAX_AGE|OPENMMO_ADMIN_[A-Z_]+|GAME_SERVER_[A-Z0-9_]+'
write_env /etc/openmmo/game.env \
  'GAME_DB_[A-Z_]+|GAME_HOST|GAME_SERVER_PORT|GAME_STATUS_[A-Z_]+|OPENMMO_SESSION_SECRET|OPENMMO_SESSION_TOKEN_MAX_AGE'

say "systemd units"
for unit in openmmo.target openmmo-db.service openmmo-login.service openmmo-game.service; do
  install -o root -g root -m 0644 "$here/systemd/$unit" "/etc/systemd/system/$unit"
done
systemctl daemon-reload
systemctl enable openmmo.target openmmo-db.service openmmo-login.service openmmo-game.service >/dev/null

# The website is its own deployment, since it also owns the static pages and the
# virtual host. Run it here so one command still installs the lot.
say "website (deploy/../web/deploy.sh)"
"$repo/web/deploy.sh" --no-build

echo
for port in 2106 7777; do
  if listening "$port" && ! holder_is_systemd "$port"; then
    warn "port $port is held by a server this script did not start:"
    ss -ltnp 2>/dev/null | grep ":$port " || true
    warn "it has live players on it. Stop it when you are ready to cut over:"
    warn "    ./kill-servers.sh && sudo systemctl start openmmo.target"
    start=0
  fi
done

if [ "$start" -eq 1 ]; then
  say "starting openmmo.target"
  systemctl start openmmo.target
fi

systemctl --no-pager --lines=0 status openmmo.target | head -3 || true
say "start with: sudo systemctl start openmmo.target"
say "stop with:  sudo systemctl stop openmmo.target"
