#!/usr/bin/env bash
# Local https for the OpenMMO website: a self-signed cert stands in for the
# Cloudflare Origin Certificate, so the Full (strict) virtual host already in
# apache/openmmo.conf switches itself on.
#
#   sudo ./web/dev-https.sh
set -euo pipefail

certdir=/etc/ssl/openmmo
host=${OPENMMO_DEV_HOST:-openmmo.dev}

[ "$(id -u)" -eq 0 ] || {
  echo "run this with sudo" >&2
  exit 1
}

install -d -m 0750 "$certdir"
if [ ! -f "$certdir/origin.pem" ]; then
  echo "==> generating a self-signed cert for $host"
  openssl req -x509 -nodes -newkey rsa:2048 -days 825 \
    -keyout "$certdir/origin.key" -out "$certdir/origin.pem" \
    -subj "/CN=$host" \
    -addext "subjectAltName=DNS:$host,DNS:www.$host,DNS:localhost,IP:127.0.0.1"
  chmod 0640 "$certdir/origin.key"
else
  echo "==> keeping the existing $certdir/origin.pem"
fi

echo "==> apache"
a2enmod ssl >/dev/null
apache2ctl configtest
systemctl reload apache2

curl -fsSk -o /dev/null -w '==> https://127.0.0.1/ %{http_code}\n' \
  --resolve "$host:443:127.0.0.1" "https://$host/"
