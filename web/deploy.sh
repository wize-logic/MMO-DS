#!/usr/bin/env bash
# Installs the OpenMMO website on this machine: the static pages into apache's
# document root, the registration service under systemd, and the apache
# virtual host that ties the two together.
#
#   sudo ./web/deploy.sh                 # build, install, reload
#   sudo ./web/deploy.sh --no-build      # skip gradle, install what is already built
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build=1
[ "${1:-}" = "--no-build" ] && build=0

docroot=/var/www/openmmo
appdir=/opt/openmmo/web
envfile=/etc/openmmo/web.env
service=openmmo-web

[ "$(id -u)" -eq 0 ] || {
  echo "run this with sudo" >&2
  exit 1
}

if [ "$build" -eq 1 ]; then
  echo "==> building server.web"
  sudo -u "${SUDO_USER:-$USER}" "$repo/gradlew" -p "$repo" :server.web:installDist --console=plain </dev/null
fi
[ -x "$repo/server.web/build/install/server.web/bin/server.web" ] || {
  echo "server.web is not built; run without --no-build" >&2
  exit 1
}

echo "==> service account and directories"
id -u openmmo-web >/dev/null 2>&1 || adduser --system --group --no-create-home openmmo-web
install -d -o root -g root -m 0755 /opt/openmmo "$docroot"
install -d -o root -g root -m 0750 /etc/openmmo

echo "==> static site -> $docroot"
# --delete would otherwise take mmo/publish.sh's update channel with it: the
# channel is published into this same document root, is not in web/public, and
# belongs to whoever builds releases rather than to root. Excluded from the
# sync and from the ownership sweep, so deploying the site cannot unpublish the
# game.
rsync -a --delete --exclude '/updates/' "$here/public/" "$docroot/"
find "$docroot" -path "$docroot/updates" -prune -o -exec chown root:root {} +
find "$docroot" -path "$docroot/updates" -prune -o -exec chmod a+rX {} +

echo "==> service -> $appdir"
rm -rf "$appdir"
cp -a "$repo/server.web/build/install/server.web" "$appdir"
chown -R root:root "$appdir"

if [ ! -f "$envfile" ]; then
  echo "==> $envfile from the repository .env"
  # The website needs the login database and nothing else out of it.
  {
    echo "# OpenMMO website. Written by web/deploy.sh, edit in place afterwards."
    echo "OPENMMO_WEB_HOST=127.0.0.1"
    echo "OPENMMO_WEB_PORT=8088"
    if [ -f "$repo/.env" ]; then
      # LOGIN_HOST/GAME_HOST as well as the ports: they bind those servers to one
      # interface, and the status panel probes whatever they name. Without them
      # it probes loopback, which a server bound to the public address refuses.
      grep -E '^(LOGIN_DB_|LOGIN_HOST=|GAME_HOST=|LOGIN_PORT=|GAME_SERVER_PORT=|GAME_STATUS_)' "$repo/.env" || true
    else
      echo "LOGIN_DB_HOST=localhost"
      echo "LOGIN_DB_PORT=20011"
      echo "LOGIN_DB_NAME=openmmo_login_db"
      echo "LOGIN_DB_USER=openmmo_login_user"
      echo "LOGIN_DB_PASSWORD=changeMe!"
    fi
    java_home="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
    echo "JAVA_HOME=$java_home"
  } >"$envfile"
  chown root:root "$envfile"
  chmod 0640 "$envfile"
else
  echo "==> keeping the existing $envfile"
fi

echo "==> systemd"
install -o root -g root -m 0644 "$here/systemd/$service.service" "/etc/systemd/system/$service.service"
systemctl daemon-reload
systemctl enable "$service" >/dev/null
systemctl restart "$service"

echo "==> apache"
a2enmod proxy proxy_http headers remoteip deflate >/dev/null
install -o root -g root -m 0644 "$here/apache/openmmo-body.conf" /etc/apache2/openmmo-body.conf
install -o root -g root -m 0644 "$here/apache/openmmo.conf" /etc/apache2/sites-available/openmmo.conf
install -o root -g root -m 0644 "$here/apache/cloudflare-remoteip.conf" \
  /etc/apache2/conf-available/cloudflare-remoteip.conf
install -o root -g root -m 0644 "$here/apache/zz-openmmo-hardening.conf" \
  /etc/apache2/conf-available/zz-openmmo-hardening.conf
a2enconf cloudflare-remoteip zz-openmmo-hardening >/dev/null
a2ensite openmmo >/dev/null
# The stock virtual host would otherwise answer for every name this one does not.
a2dissite 000-default >/dev/null 2>&1 || true
apache2ctl configtest
systemctl reload apache2

echo
systemctl --no-pager --lines=0 status "$service" | head -4

# The jvm takes a couple of seconds to open its socket, and apache answers 503
# until it does. Wait for it rather than reporting a startup race as a failure.
for _ in $(seq 30); do
  curl -fsS -o /dev/null http://127.0.0.1:8088/api/health 2>/dev/null && break
  sleep 1
done
curl -fsS -o /dev/null -w '==> http://127.0.0.1/ %{http_code}\n' http://127.0.0.1/
curl -fsS -o /dev/null -w '==> http://127.0.0.1/api/status %{http_code}\n' http://127.0.0.1/api/status
