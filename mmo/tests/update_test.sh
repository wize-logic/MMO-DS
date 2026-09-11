#!/bin/sh
# The download half of the feed, end to end.
set -u

ROOT=${1:?usage: update_test.sh <mmo-root> <build-dir>}
BUILD=${2:?usage: update_test.sh <mmo-root> <build-dir>}

LAUNCHER="$BUILD/openmmo-launch"
echo "the launcher fetches only what the operator signed:"
if [ ! -x "$LAUNCHER" ]; then
    echo "  SKIP no launcher at $LAUNCHER"
    exit 0
fi
for cmd in python3 openssl sha256sum; do
    if ! command -v $cmd > /dev/null 2>&1; then
        echo "  SKIP $cmd is not installed"
        exit 0
    fi
done

fail=0
tmp=$(mktemp -d)
server_pid=''
# The https servers below are started inside a command substitution, whose
# variables never reach this shell, so their pids are written to a file the
# trap reads: a `pids` variable here stayed empty and leaked one server per
# check.
trap '[ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null; [ -f "$tmp/pids" ] && kill $(cat "$tmp/pids") 2>/dev/null; rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# ------------------------------------------------------- the operator's side
mkdir -p "$tmp/pub/bin" "$tmp/pub/res"
printf 'the game, v5\n' > "$tmp/pub/bin/game"
chmod +x "$tmp/pub/bin/game"
printf 'some data, v5\n' > "$tmp/pub/res/data.bin"
printf '5\n' > "$tmp/pub/revision.txt"

openssl genrsa -out "$tmp/private.pem" 2048 2> /dev/null || exit 1
openssl rsa -in "$tmp/private.pem" -pubout -out "$tmp/feed-key.pem" 2> /dev/null || exit 1

if "$ROOT/feedgen.sh" "$tmp/pub" "$tmp/private.pem" "$tmp/out" > /dev/null; then
    ok "feedgen signs the install into a channel"
else
    bad "feedgen signs the install into a channel"
    exit 1
fi
if openssl dgst -sha256 -verify "$tmp/feed-key.pem" \
       -signature "$tmp/out/update_feed.sig256" \
       "$tmp/out/update_feed.txt" > /dev/null 2>&1; then
    ok "openssl agrees the signature is the key's"
else
    bad "openssl agrees the signature is the key's"
fi

# A stock static file server, on whatever port is free; the launcher is told
# the port the server printed.
python3 -u -m http.server --bind 127.0.0.1 --directory "$tmp/out" 0 \
    > "$tmp/server.log" 2>&1 &
server_pid=$!
port=''
for _ in $(seq 1 50); do
    port=$(sed -n 's/.*port \([0-9]*\).*/\1/p' "$tmp/server.log" | head -1)
    [ -n "$port" ] && break
    sleep 0.1
done
if [ -z "$port" ]; then
    # The signature checks above already ran, so leaving with 0 here would
    # throw away a real failure on the way out. Skip the rest, keep the verdict.
    echo "  SKIP python's http.server did not start"
    exit "$fail"
fi

# --------------------------------------------------------- the player's side
mkdir -p "$tmp/root/bin"
printf 'the game, v4\n' > "$tmp/root/bin/game"      # stale
printf '4\n' > "$tmp/root/revision.txt"             # behind
                                                    # res/data.bin: missing
printf 'not a rom, but readable\n' > "$tmp/rom.nds"
cat > "$tmp/launcher.cfg" <<EOF
rom $tmp/rom.nds
feed $tmp/root/feed
feed-key $tmp/feed-key.pem
feed-url http://127.0.0.1:$port
EOF

run() { OPENMMO_ROOT="$tmp/root" "$LAUNCHER" --config "$tmp/launcher.cfg" "$@"; }

if run --update > "$tmp/update.log" 2>&1; then
    ok "a stale install updates against the served channel"
else
    bad "a stale install updates against the served channel"
    sed 's/^/       /' "$tmp/update.log"
fi
for f in bin/game res/data.bin revision.txt; do
    if cmp -s "$tmp/pub/$f" "$tmp/root/$f"; then
        ok "$f is byte-identical to what was published"
    else
        bad "$f is byte-identical to what was published"
    fi
done
if [ -x "$tmp/root/bin/game" ]; then
    ok "the executable bit followed the feed"
else
    bad "the executable bit followed the feed"
fi
if [ -f "$tmp/root/feed/update_feed.sig256" ]; then
    ok "the verified documents became the local feed"
else
    bad "the verified documents became the local feed"
fi
if [ ! -d "$tmp/root/update.staging" ]; then
    ok "staging is gone once the install is written"
else
    bad "staging is gone once the install is written"
fi

if run --update 2>&1 | grep -q "current at revision 5"; then
    ok "a second update says the install is current"
else
    bad "a second update says the install is current"
fi

# .forceupdate beside the install turns the next update into a full repair:
# every file fetched and proven again, current or not, and the trigger is
# spent only when that succeeds.
touch "$tmp/root/.forceupdate"
if run --update > "$tmp/force.log" 2>&1 \
   && grep -q "updated 3 files" "$tmp/force.log" \
   && [ ! -f "$tmp/root/.forceupdate" ]; then
    ok "a .forceupdate repairs a current install and is spent by success"
else
    bad "a .forceupdate repairs a current install and is spent by success"
    sed 's/^/       /' "$tmp/force.log"
fi

# ------------------------------------------------------- asking, not fetching
if run --latest 2>&1 | grep -q "this build is current at r5"; then
    ok "--latest says an install at the channel's revision is current"
else
    bad "--latest says an install at the channel's revision is current"
fi

printf '4\n' > "$tmp/root/revision.txt"             # behind again
if run --latest 2>&1 | grep -q "a newer build is published: r5, and this one is r4"; then
    ok "--latest names the newer revision and this one"
else
    bad "--latest names the newer revision and this one"
fi
if [ "$(cat "$tmp/root/revision.txt")" = "4" ] \
   && cmp -s "$tmp/pub/bin/game" "$tmp/root/bin/game"; then
    ok "asking changed nothing on disk"
else
    bad "asking changed nothing on disk"
fi

printf ' ' >> "$tmp/out/main_feed.txt"              # unsigned byte
if run --latest > "$tmp/latest.log" 2>&1; then
    bad "--latest refuses a main feed the key did not sign"
else
    if grep -q "not signed by a key this client trusts" "$tmp/latest.log"; then
        ok "--latest refuses a main feed the key did not sign"
    else
        bad "--latest refuses a main feed the key did not sign (wrong reason)"
        sed 's/^/       /' "$tmp/latest.log"
    fi
fi
truncate -s -1 "$tmp/out/main_feed.txt"             # the operator's document back
printf '5\n' > "$tmp/root/revision.txt"

# ------------------------------------------------------ the app's own update
mkdir -p "$tmp/pubapk"
head -c 300000 /dev/urandom > "$tmp/pubapk/openmmo-r7-android-armeabi-v7a.apk"
printf '7\n' > "$tmp/pubapk/revision.txt"
if "$ROOT/feedgen.sh" "$tmp/pubapk" "$tmp/private.pem" "$tmp/out/android" > /dev/null; then
    ok "feedgen signs the android channel"
else
    bad "feedgen signs the android channel"
fi
cat > "$tmp/launcher-apk.cfg" <<EOF
rom $tmp/rom.nds
feed $tmp/root/feed
feed-key $tmp/feed-key.pem
feed-url http://127.0.0.1:$port/android
EOF
runapk() { OPENMMO_ROOT="$tmp/root" "$LAUNCHER" --config "$tmp/launcher-apk.cfg" "$@"; }

if runapk --fetch-package "$tmp/got.apk" > "$tmp/apk.log" 2>&1 \
   && grep -q "openmmo-r7-android-armeabi-v7a.apk is r7" "$tmp/apk.log"; then
    ok "--fetch-package brings the channel's apk down and names its revision"
else
    bad "--fetch-package brings the channel's apk down and names its revision"
    sed 's/^/       /' "$tmp/apk.log"
fi
if cmp -s "$tmp/pubapk/openmmo-r7-android-armeabi-v7a.apk" "$tmp/got.apk"; then
    ok "the fetched apk is byte-identical to what was published"
else
    bad "the fetched apk is byte-identical to what was published"
fi

# The same length, so the hash is what refuses it; and nothing may be left
# at the destination, because the next step would hand that file to an
# installer.
head -c 300000 /dev/urandom > "$tmp/out/android/files/openmmo-r7-android-armeabi-v7a.apk"
rm -f "$tmp/got.apk"
if runapk --fetch-package "$tmp/got.apk" > "$tmp/apk-evil.log" 2>&1; then
    bad "an apk whose bytes are not the signed ones is refused"
else
    if grep -q "downloaded wrong" "$tmp/apk-evil.log" && [ ! -f "$tmp/got.apk" ]; then
        ok "an apk whose bytes are not the signed ones is refused and removed"
    else
        bad "an apk whose bytes are not the signed ones is refused and removed"
        sed 's/^/       /' "$tmp/apk-evil.log"
    fi
fi

# A channel with no package in it says so rather than fetching something else.
if run --fetch-package "$tmp/got.apk" > "$tmp/apk-none.log" 2>&1; then
    bad "a channel with no apk is refused by name"
else
    if grep -q "carries no .apk" "$tmp/apk-none.log"; then
        ok "a channel with no apk is refused by name"
    else
        bad "a channel with no apk is refused by name (wrong reason)"
        sed 's/^/       /' "$tmp/apk-none.log"
    fi
fi

# The gate cannot be held to bin/game (the launch target is the packaged game
# binary, which this fixture does not stage), so only the update half runs
# here; feed_test.c and the gate's own suite hold the rest.

# ------------------------------------------------------------- the refusals
printf ' ' >> "$tmp/out/update_feed.txt"            # unsigned byte
if run --update > "$tmp/tamper.log" 2>&1; then
    bad "a feed the key did not sign moves nothing"
else
    if grep -q "not signed by a key this launcher trusts" "$tmp/tamper.log"; then
        ok "a feed the key did not sign moves nothing"
    else
        bad "a feed the key did not sign moves nothing (wrong reason)"
        sed 's/^/       /' "$tmp/tamper.log"
    fi
fi
# putting the byte back restores the operator's document exactly
truncate -s -1 "$tmp/out/update_feed.txt"

# The same length as the real file, so the hash check is what refuses it
# rather than the size cap in front of it.
printf 'the game, v6\n' > "$tmp/out/files/bin/game"
printf 'the game, v4\n' > "$tmp/root/bin/game"      # stale again
if run --update > "$tmp/evil.log" 2>&1; then
    bad "a payload whose bytes are not the signed ones moves nothing"
else
    if grep -q "downloaded wrong" "$tmp/evil.log"; then
        ok "a payload whose bytes are not the signed ones moves nothing"
    else
        bad "a payload whose bytes are not the signed ones moves nothing (wrong reason)"
        sed 's/^/       /' "$tmp/evil.log"
    fi
fi
if cmp -s "$tmp/root/bin/game" "$tmp/pub/bin/game"; then
    bad "the stale file stayed stale rather than turning evil"
else
    if printf 'the game, v4\n' | cmp -s - "$tmp/root/bin/game"; then
        ok "the stale file stayed stale rather than turning evil"
    else
        bad "the stale file stayed stale rather than turning evil"
    fi
fi

# ------------------------------------------------------------------- https
cp "$tmp/pub/bin/game" "$tmp/out/files/bin/game"
cat > "$tmp/serve.py" <<'PYEOF'
import functools, http.server, ssl, sys
# serve.py <dir> [<cert> <key>]                  a static server, https with a pair
# serve.py redirect <target> [<cert> <key>]      every GET is a 301 to target+path
class Redirect(http.server.BaseHTTPRequestHandler):
    target = ''
    def do_GET(self):
        self.send_response(301)
        self.send_header('Location', self.target + self.path)
        self.send_header('Content-Length', '0')
        self.end_headers()
    def log_message(self, *a):
        pass
if sys.argv[1] == 'redirect':
    Redirect.target = sys.argv[2]
    handler = Redirect
    pair = sys.argv[3:5]
else:
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=sys.argv[1])
    pair = sys.argv[2:4]
srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
if pair:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(pair[0], pair[1])
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
print('port', srv.server_address[1], flush=True)
srv.serve_forever()
PYEOF
serve() {  # serve <log> <serve.py args...>: echoes the port it took, or nothing
    log=$1; shift
    python3 -u "$tmp/serve.py" "$@" > "$log" 2>&1 &
    echo $! >> "$tmp/pids"
    p=''
    for _ in $(seq 1 50); do
        p=$(sed -n 's/^port \([0-9]*\)$/\1/p' "$log" | head -1)
        [ -n "$p" ] && break
        sleep 0.1
    done
    echo "$p"
}
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout "$tmp/tls.key" -out "$tmp/tls.crt" -days 2 -subj /CN=openmmo-test \
    -addext subjectAltName=IP:127.0.0.1,DNS:localhost > /dev/null 2>&1 || exit 1
sport=$(serve "$tmp/https.log" "$tmp/out" "$tmp/tls.crt" "$tmp/tls.key")
if [ -z "$sport" ]; then
    echo "  SKIP python's https server did not start"
else
    printf 'the game, v4\n' > "$tmp/root/bin/game"      # stale again
    sed -i "s|^feed-url .*|feed-url https://127.0.0.1:$sport|" "$tmp/launcher.cfg"
    printf 'feed-ca %s\n' "$tmp/tls.crt" >> "$tmp/launcher.cfg"
    if run --update > "$tmp/tls-update.log" 2>&1 \
       && cmp -s "$tmp/pub/bin/game" "$tmp/root/bin/game"; then
        ok "an https channel updates under the authority feed-ca names"
    else
        bad "an https channel updates under the authority feed-ca names"
        sed 's/^/       /' "$tmp/tls-update.log"
    fi

    # Without the row nothing vouches for a certificate that signed itself.
    sed -i '/^feed-ca /d' "$tmp/launcher.cfg"
    if run --latest > "$tmp/tls-untrusted.log" 2>&1; then
        bad "a certificate no trusted root signed is refused"
    else
        if grep -q "certificate is not trusted" "$tmp/tls-untrusted.log"; then
            ok "a certificate no trusted root signed is refused"
        else
            bad "a certificate no trusted root signed is refused (wrong reason)"
            sed 's/^/       /' "$tmp/tls-untrusted.log"
        fi
    fi

    # A proxy that forces https answers a plain URL with a redirect. The fetch
    # follows it up, and only up.
    printf 'feed-ca %s\n' "$tmp/tls.crt" >> "$tmp/launcher.cfg"
    rport=$(serve "$tmp/redirect.log" redirect "https://127.0.0.1:$sport")
    sed -i "s|^feed-url .*|feed-url http://127.0.0.1:$rport|" "$tmp/launcher.cfg"
    if run --latest 2>&1 | grep -q "this build is current at r5"; then
        ok "a forced-https redirect is followed"
    else
        bad "a forced-https redirect is followed"
    fi
    dport=$(serve "$tmp/downgrade.log" redirect "http://127.0.0.1:$port" \
                  "$tmp/tls.crt" "$tmp/tls.key")
    sed -i "s|^feed-url .*|feed-url https://127.0.0.1:$dport|" "$tmp/launcher.cfg"
    if run --latest > "$tmp/tls-downgrade.log" 2>&1; then
        bad "a redirect from https back to plain http is refused"
    else
        if grep -q "redirects to plain http" "$tmp/tls-downgrade.log"; then
            ok "a redirect from https back to plain http is refused"
        else
            bad "a redirect from https back to plain http is refused (wrong reason)"
            sed 's/^/       /' "$tmp/tls-downgrade.log"
        fi
    fi
    sed -i '/^feed-ca /d' "$tmp/launcher.cfg"
    sed -i "s|^feed-url .*|feed-url http://127.0.0.1:$port|" "$tmp/launcher.cfg"
fi

# A .forceupdate that has nowhere to fetch from is said out loud, because a
# copied working-tree build has no channel and silence there reads as the
# trigger being broken rather than the build being unpinned.
rm -rf "$tmp/root3"
mkdir -p "$tmp/root3"
touch "$tmp/root3/.forceupdate"
printf 'rom %s\n' "$tmp/rom.nds" > "$tmp/plain.cfg"
if OPENMMO_ROOT="$tmp/root3" "$LAUNCHER" --config "$tmp/plain.cfg" --update \
       2>&1 | grep -q "no update URL"; then
    ok "a .forceupdate with no channel configured is named, not ignored"
else
    bad "a .forceupdate with no channel configured is named, not ignored"
fi

# ------------------------------------------------- the compiled-in channel
# A release bakes the URL and the key (feed_pin.h) so a player configures
# nothing. Build a throwaway launcher pinned at this test's server and hand
# it a settings file that names no feed at all: a bare install must fill
# itself and leave the verified feed under the install root.
cp "$tmp/pub/bin/game" "$tmp/out/files/bin/game"
rm -rf "$tmp/root2"
mkdir -p "$tmp/root2"
printf 'rom %s\n' "$tmp/rom.nds" > "$tmp/pinned.cfg"
if make -C "$ROOT" launcher LAUNCHER="$tmp/openmmo-launch-pinned" \
        FEEDPIN="$tmp/gen/feed_pin.h" \
        FEED_URL="http://127.0.0.1:$port" FEED_KEY="$tmp/feed-key.pem" \
        > "$tmp/pinned-build.log" 2>&1; then
    if OPENMMO_ROOT="$tmp/root2" "$tmp/openmmo-launch-pinned" \
           --config "$tmp/pinned.cfg" --update > "$tmp/pinned.log" 2>&1 \
       && cmp -s "$tmp/pub/bin/game" "$tmp/root2/bin/game" \
       && [ -f "$tmp/root2/feed/update_feed.sig256" ]; then
        ok "a pinned launcher updates with nothing configured"
    else
        bad "a pinned launcher updates with nothing configured"
        sed 's/^/       /' "$tmp/pinned.log" 2>/dev/null
    fi
else
    bad "a pinned launcher builds (make launcher FEED_URL=... FEED_KEY=...)"
    tail -5 "$tmp/pinned-build.log" | sed 's/^/       /'
fi

# ------------------------------------------------ what the publish reads back
# publish.sh ends by driving this same --update against a throwaway root over
# the address it baked, and it decides whether the channel is live by taking
# the revision out of the last line.
rm -rf "$tmp/root4"
mkdir -p "$tmp/root4"
OPENMMO_ROOT="$tmp/root4" "$LAUNCHER" --config "$tmp/launcher.cfg" --update \
    > "$tmp/empty.log" 2>&1
got=$(tail -1 "$tmp/empty.log" | sed -n 's/.*revision \([0-9]*\).*/\1/p')
if [ "$got" = "5" ] && [ -f "$tmp/root4/res/data.bin" ]; then
    ok "an empty root fills from the channel and its last line names the revision"
else
    bad "an empty root fills from the channel and its last line names the revision"
    sed 's/^/       /' "$tmp/empty.log"
fi

# ------------------------------------------------------ replacing the running
# The one file an update cannot simply replace is the launcher's own binary,
# which is running while it is being replaced: it steps aside to `.old` and
# the new image takes its name.
mkdir -p "$tmp/pub5/bin" "$tmp/root5/bin"
cp "$LAUNCHER" "$tmp/pub5/bin/openmmo-launch"
printf 'a byte the installed copy does not have\n' >> "$tmp/pub5/bin/openmmo-launch"
printf '5\n' > "$tmp/pub5/revision.txt"
cp "$LAUNCHER" "$tmp/root5/bin/openmmo-launch"
port5=''
if "$ROOT/feedgen.sh" "$tmp/pub5" "$tmp/private.pem" "$tmp/out5" > /dev/null; then
    port5=$(serve "$tmp/self-server.log" "$tmp/out5")
fi
if [ -z "$port5" ]; then
    echo "  SKIP no server for the launcher's own channel"
else
    printf 'rom %s\nfeed %s/root5/feed\nfeed-key %s/feed-key.pem\nfeed-url http://127.0.0.1:%s\n' \
        "$tmp/rom.nds" "$tmp" "$tmp" "$port5" > "$tmp/self.cfg"
    swap() { "$tmp/root5/bin/openmmo-launch" --config "$tmp/self.cfg" --update; }
    if swap > "$tmp/self1.log" 2>&1 \
       && grep -q "the launcher itself among them" "$tmp/self1.log" \
       && cmp -s "$tmp/pub5/bin/openmmo-launch" "$tmp/root5/bin/openmmo-launch" \
       && [ -f "$tmp/root5/bin/openmmo-launch.old" ]; then
        ok "the running launcher steps aside and the published one takes its name"
    else
        bad "the running launcher steps aside and the published one takes its name"
        sed 's/^/       /' "$tmp/self1.log"
    fi
    touch "$tmp/root5/.forceupdate"
    if swap > "$tmp/self2.log" 2>&1 \
       && cmp -s "$tmp/pub5/bin/openmmo-launch" "$tmp/root5/bin/openmmo-launch" \
       && [ ! -f "$tmp/root5/.forceupdate" ]; then
        ok "and a repair does it again over the .old an earlier one left"
    else
        bad "and a repair does it again over the .old an earlier one left"
        sed 's/^/       /' "$tmp/self2.log"
    fi
fi

# --------------------------------------------------- the page beside the feed
# The wording is the release's promise to a player, so it is held here rather
# than only read: what the page says about the three cartridges, and about
# what an offline save may and may not do once it is online, is the same claim
# the launcher and the server enforce.
mkdir -p "$tmp/dest"
printf 'a zip\n' > "$tmp/dest/openmmo-linux.zip"
printf 'a stray versioned name from an earlier publish\n' > "$tmp/dest/openmmo-v8-linux-x86_64.zip"
zipsha=$(sha256sum "$tmp/dest/openmmo-linux.zip" | cut -d' ' -f1)
if "$ROOT/downloadpage.sh" "$tmp/dest" v9 5 > /dev/null; then
    page=$(cat "$tmp/dest/download.html")
    ok "the download page is written from what the publish left in the folder"
    case $page in
        *'href="openmmo-linux.zip?'"$zipsha"'"'*) ok "and it links the download that is there, by its stable name, hash-busted";;
        *) bad "and it links the download that is there, by its stable name, hash-busted";;
    esac
    case $page in
        *openmmo-v8-*) bad "and a versioned name is never offered";;
        *) ok "and a versioned name is never offered";;
    esac
    case $page in
        *"$zipsha"*) ok "and shows the archive's own hash";;
        *) bad "and shows the archive's own hash";;
    esac
    miss=''
    for word in Platinum HeartGold Black offline ranked GTL Sinnoh; do
        case $page in *"$word"*) ;; *) miss="$miss $word";; esac
    done
    if [ -z "$miss" ]; then
        ok "and it still says offline play, the fence, and the three cartridges"
    else
        bad "the download page no longer mentions:$miss"
    fi
else
    bad "the download page is written from what the publish left in the folder"
fi
mkdir -p "$tmp/dest-empty"
if "$ROOT/downloadpage.sh" "$tmp/dest-empty" v10 5 > /dev/null 2>&1; then
    bad "a folder with nothing to download is refused rather than advertised"
else
    ok "a folder with nothing to download is refused rather than advertised"
fi

exit $fail
