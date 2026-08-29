#!/usr/bin/env bash
# mmo/feedgen.sh, an install tree, turned into a signed update channel.
#
#   OUT_DIR/
#     main_feed.txt        server address, revision, revision floor
#     main_feed.sig256     the operator's signature over it
#     update_feed.txt      every file: name, sha256, size, executable
#     update_feed.sig256   the operator's signature over it
#     files/<name>         the install's files, at their feed names
#   openssl genrsa -out feed_private.pem 3072
#   openssl rsa -in feed_private.pem -pubout -out feed-key.pem

set -u

usage() {
    printf 'usage: feedgen.sh INSTALL_DIR PRIVATE_KEY OUT_DIR [options]\n'
    printf '  --ip HOST         main_feed <ip> (default: none)\n'
    printf '  --port N          main_feed <port> (default: 2106)\n'
    printf '  --min-revision N  the revision floor (default: 0)\n'
    printf '  --min-launcher N  min_launcher_version (default: 1)\n'
    exit 2
}

[ $# -ge 3 ] || usage
INSTALL=$1; KEY=$2; OUT=$3; shift 3
IP=''; PORT=2106; MINREV=0; MINLAUNCH=1
while [ $# -gt 0 ]; do
    case $1 in
        --ip)           IP=$2; shift 2 ;;
        --port)         PORT=$2; shift 2 ;;
        --min-revision) MINREV=$2; shift 2 ;;
        --min-launcher) MINLAUNCH=$2; shift 2 ;;
        *) usage ;;
    esac
done

fail() { printf 'feedgen: %s\n' "$1" >&2; exit 1; }

[ -d "$INSTALL" ] || fail "no install tree at $INSTALL"
[ -f "$KEY" ] || fail "no private key at $KEY (openssl genrsa -out $KEY 3072)"
command -v openssl >/dev/null 2>&1 || fail "openssl is not installed"

# The install revision is the one package.sh wrote; a channel without one
# would ask every client for a full update on every check.
[ -f "$INSTALL/revision.txt" ] || fail "$INSTALL/revision.txt is missing"
REV=$(tr -cd '0-9' < "$INSTALL/revision.txt")
[ -n "$REV" ] || fail "$INSTALL/revision.txt holds no revision number"

# Every file of the install, at its feed name. The C reader caps the
# inventory at 512 entries and treats overflow as failure, so this does too.
FILES=$(cd "$INSTALL" && find . -type f | sed 's|^\./||' | LC_ALL=C sort)
COUNT=$(printf '%s\n' "$FILES" | wc -l)
[ "$COUNT" -le 512 ] || fail "$COUNT files, and the launcher holds 512"

xml_escape() {
    sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' \
        -e 's/"/\&quot;/g' -e "s/'/\\&apos;/g"
}

mkdir -p "$OUT/files" || fail "cannot write $OUT"

{
    printf '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
    printf '<main_feed>\n'
    [ -n "$IP" ] && printf '  <ip>%s</ip>\n' "$(printf '%s' "$IP" | xml_escape)"
    printf '  <port>%s</port>\n' "$PORT"
    printf '  <revision>%s</revision>\n' "$REV"
    printf '  <min_revision>%s</min_revision>\n' "$MINREV"
    printf '</main_feed>\n'
} > "$OUT/main_feed.txt" || fail "cannot write $OUT/main_feed.txt"

{
    printf '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
    printf '<update_feed min_launcher_version="%s">\n' "$MINLAUNCH"
    printf '%s\n' "$FILES" | while IFS= read -r name; do
        path="$INSTALL/$name"
        sha=$(sha256sum "$path" | cut -d' ' -f1) || exit 1
        size=$(stat -c %s "$path" 2>/dev/null || stat -f %z "$path") || exit 1
        exe=''
        [ -x "$path" ] && exe=' executable="true"'
        printf '  <file name="%s" sha256="%s" size="%s"%s/>\n' \
            "$(printf '%s' "$name" | xml_escape)" "$sha" "$size" "$exe"
    done || exit 1
    printf '</update_feed>\n'
} > "$OUT/update_feed.txt" || fail "cannot write $OUT/update_feed.txt"

# A pipeline stage's exit does not fail the block above on every shell, so
# hold the document to the install by count before signing anything.
LISTED=$(grep -c '<file ' "$OUT/update_feed.txt")
[ "$LISTED" -eq "$COUNT" ] || fail "wrote $LISTED of $COUNT entries"

for doc in main_feed update_feed; do
    openssl dgst -sha256 -sign "$KEY" -out "$OUT/$doc.sig256" \
        "$OUT/$doc.txt" || fail "cannot sign $doc.txt"
done

# The payload, at the names the feed vouches for.
printf '%s\n' "$FILES" | while IFS= read -r name; do
    mkdir -p "$OUT/files/$(dirname "$name")" || exit 1
    cp -p "$INSTALL/$name" "$OUT/files/$name" || exit 1
done || fail "cannot copy the install into $OUT/files"

printf 'feedgen: revision %s, %s files, signed into %s\n' \
    "$REV" "$COUNT" "$OUT"
printf 'feedgen: serve that directory over plain http; the launcher wants\n'
printf 'feedgen:   feed-url http://your.host/path-to-it\n'
