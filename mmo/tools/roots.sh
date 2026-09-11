#!/bin/sh
# Refresh third_party/cacert.pem, the root certificates the update
# fetch trusts, from the bundle curl publishes out of Mozilla's store,
# checking the sha256 published beside it.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://curl.se/ca/cacert.pem

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

curl -fsSL -o "$tmp/cacert.pem" "$URL"
curl -fsSL -o "$tmp/cacert.pem.sha256" "$URL.sha256"
if ! (cd "$tmp" && sha256sum -c cacert.pem.sha256 > /dev/null); then
    echo "roots: the bundle does not match the sha256 published beside it" >&2
    exit 1
fi
n=$(grep -c 'BEGIN CERTIFICATE' "$tmp/cacert.pem")
if [ "$n" -lt 100 ]; then
    echo "roots: only $n certificates in the bundle; refusing it" >&2
    exit 1
fi
cp "$tmp/cacert.pem" "$ROOT/third_party/cacert.pem"
printf 'roots: %s certificates, Mozilla data of %s\n' "$n" \
    "$(sed -n 's/^## Certificate data from Mozilla as of: //p' "$ROOT/third_party/cacert.pem")"
