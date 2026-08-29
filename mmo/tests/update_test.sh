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
trap '[ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null; rm -rf "$tmp"' EXIT

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
    echo "  SKIP python's http.server did not start"
    exit 0
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

sed -i "s|feed-url http://|feed-url https://|" "$tmp/launcher.cfg"
if run --update > "$tmp/https.log" 2>&1; then
    bad "an https URL is refused by name"
else
    if grep -q "speaks plain http" "$tmp/https.log"; then
        ok "an https URL is refused by name"
    else
        bad "an https URL is refused by name (wrong reason)"
        sed 's/^/       /' "$tmp/https.log"
    fi
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

exit $fail
