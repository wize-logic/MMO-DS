#!/usr/bin/env bash
# mmo/publish.sh, one command from this tree to a folder your web server
# serves: build both releases the way dist.sh does, sign each install into its
# update channel, and lay everything out where players and their launchers
# will fetch it.
#
#   mmo/publish.sh PRIVATE_KEY DEST_DIR URL
#              3072, once, and it never enters the repository or DEST).
#              A relative path is read from where you are standing, which is
#              not necessarily where you made the key.
#              what it replaces; a site sharing the folder is left alone.
#              It has to be writable by the account that builds, a web root
#              usually is not, and the answer is to own the one folder:
#                  sudo mkdir -p DEST && sudo chown "$USER" DEST
#              never to run this under sudo. Everything after the checks is a
#              build, and root's build leaves root-owned objects in mmo/build
#              that your next ordinary build cannot overwrite. Both are
#              refused before the build starts rather than after it.
#              for a server on a laptop). It is baked into the launchers
#              (feed_pin.h) beside the server pin, so a player configures
#              Nothing: unzip, run, and every Play fetches <URL>/linux or
#              <URL>/windows. The signature is the trust either way
#; an https channel is verified against the roots
#              the launcher compiles in and needs no rule at the proxy. One
#              Carry-over: a launcher from before 2026-09-05 speaks plain
#              http only and refuses a redirect, so the http path has to
#              stay open until every installed one has updated itself once.
#              The read-back at the end says whether it still is.
#   linux/ windows/ android/          the signed channels the clients poll
#   openmmo-windows.zip               the first install a player downloads,
#   openmmo-linux.zip                 with the channel and key baked in. No
#   openmmo-android.apk               revision in the name: a player picks
#                                     "Windows", not r1186 (owner, 2026-09-07);
#                                     the version is the page's footer and
#                                     the archive's own revision.txt. The
#                                     versioned copies stay in build/dist
#   openmmo-*.sha256                  the archive's digest, beside each, in
#                                     sha256sum's format under the same name
#   feed-key.pem                      the public key, for anyone configuring
#                                     a build-tree launcher by hand
#   download.html                     the page a player lands on, listing the
#                                     downloads that are actually there. It is
#                                     download.html and not index.html because
#                                     index.html belongs to whoever owns the
#                                     folder; link it or serve it as the index
#   --host linux|windows|android   just the one (repeat it to name a set)
#   --version V            name the release yourself (default: git describe)
#   --min-revision N       the repair floor feedgen writes (default: 0)
#   --jobs N               make -j (default: dist.sh's own)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

[[ $# -ge 3 ]] || { usage; exit 2; }
KEY=$1; DEST=$2; URL=$3; shift 3
HOSTS=(linux windows android); VERSION=""; MINREV=0; JOBS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)         if [[ "${HOSTS_CHOSEN:-0}" -eq 0 ]]; then
                            HOSTS=("$2"); HOSTS_CHOSEN=1
                        else
                            HOSTS+=("$2")
                        fi
                        shift 2;;
        --version)      VERSION="$2"; shift 2;;
        --min-revision) MINREV="$2"; shift 2;;
        --jobs|-j)      JOBS="$2"; shift 2;;
        -h|--help)      usage; exit 0;;
        *) printf 'publish: unknown option %s (try --help)\n' "$1" >&2; exit 2;;
    esac
done

say()  { printf '\033[1mpublish:\033[0m %s\n' "$1"; }
warn() { printf '\033[33mpublish:\033[0m %s\n' "$1" >&2; }
bad()  { printf '\033[31mpublish:\033[0m %s\n' "$1" >&2; }

if [[ ! -f "$KEY" ]]; then
    bad "no private key at $(realpath -m -- "$KEY")"
    bad "the path is read from where you are standing, not from the repository"
    bad "root, make one once with: openssl genrsa -out $KEY 3072"
    exit 1
fi
case "$URL" in
    https://*) ;;
    http://*)  warn "$URL is plain http: every fetch a player's launcher makes"
               warn "will be readable on the path. Fine for a server on a laptop;"
               warn "a release bakes an https:// address (mmo/FEED.md)";;
    *) bad "$URL is not an https:// or http:// URL"; exit 1;;
esac
command -v openssl >/dev/null || { bad "no openssl"; exit 1; }
command -v rsync   >/dev/null || { bad "no rsync, apt install rsync"; exit 1; }
URL="${URL%/}"

# Root is refused when it is somebody else's sudo, because the build is what
# runs next: root's make writes root-owned objects into that person's
# mmo/build, and their next ordinary build cannot overwrite them.
#
#   sudo mkdir -p DEST && sudo chown "$USER" DEST
if [[ ${EUID:-$(id -u)} -eq 0 && -n "${SUDO_USER:-}" ]]; then
    bad "do not sudo this: the build would write root-owned objects into"
    bad "$SUDO_USER's mmo/build. Give yourself the destination instead:"
    bad "  sudo mkdir -p $DEST && sudo chown $SUDO_USER $DEST"
    exit 1
fi

# The destination is proved writable before the build, not after it. dist.sh
# builds, packages, unpacks and starts two hosts; finding out at the end of
# all that that the web root belongs to root is the whole build spent for
# nothing, which is exactly how this script used to fail.
if [[ ! -d "$DEST" ]] && ! mkdir -p "$DEST" 2>/dev/null; then
    bad "cannot create $DEST (its parent belongs to someone else), run"
    bad "  sudo mkdir -p $DEST && sudo chown $(id -un) $DEST"
    exit 1
fi
if ! probe="$(mktemp "$DEST/.publish-probe-XXXXXX" 2>/dev/null)"; then
    bad "$DEST is not writable by $(id -un) -- run"
    bad "  sudo chown $(id -un) $DEST"
    exit 1
fi
rm -f "$probe"

# The ANDROID TOOLCHAIN is proved before the build, for the same reason DEST
# is proved above it: dist.sh builds, packages and checks every host it was
# given, and finding out at the end of all that that this machine has no NDK
# is the whole build spent for nothing.
for h in "${HOSTS[@]}"; do
    [[ "$h" == android ]] || continue
    missing=""
    [[ -d "${NDK:-$HOME/.local/opt/android-ndk-r27c}" ]] \
        || missing="the NDK at ${NDK:-$HOME/.local/opt/android-ndk-r27c}"
    if [[ ! -x "${APKSIGNER:-$HOME/.local/opt/android-sdk/build-tools/35.0.0/apksigner}" ]]; then
        missing="${missing:+$missing and }apksigner"
    fi
    # The third dependency, and the one nothing else names: a static armeabi-v7a
    # FreeType. Makefile.android reads its headers into VIEWOBJS and links
    # lib/libfreetype.a straight onto EXTRA_FUSED_OBJS, so a machine without it
    # gets all the way through the app objects and dies at the fused link,
    # which is exactly the whole-build-spent-for-nothing this block exists to
    # prevent, and it went unchecked because the two machines that build
    # android already had one. Found 2026-08-28 on a fresh checkout.
    if [[ ! -f "${FT_ANDROID:-$HOME/.local/opt/freetype-android}/lib/libfreetype.a" ]]; then
        missing="${missing:+$missing and }a static armeabi-v7a FreeType at ${FT_ANDROID:-$HOME/.local/opt/freetype-android}"
    fi
    if [[ -n "$missing" ]]; then
        bad "the android release needs $missing, which this machine does not have."
        bad "Publish the desktop pair alone with:"
        bad "    $0 $KEY $DEST $URL --host linux --host windows"
        bad "(mmo/Makefile.android's header says where the NDK and SDK go; its"
        bad " FT_ANDROID says where the FreeType goes.)"
        exit 1
    fi
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/openmmo-publish-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT INT TERM

# The public half, derived rather than asked for: two key arguments would be
# two chances to hand the wrong pair out.
openssl rsa -in "$KEY" -pubout -out "$WORK/feed-key.pem" 2>/dev/null \
    || { bad "$KEY is not an RSA private key"; exit 1; }

# dist.sh builds, packages, unpacks and starts both releases, with the
# channel baked in; --keep leaves the staged install trees under our own
# TMPDIR, which is where feedgen reads them from.
say "building and packaging through dist.sh"
# Resolved here and passed down, because the staged folder is plain `openmmo`
# now (a folder that names a revision lies as soon as the install updates) and
# so no longer carries the version to read back. Same rule dist.sh applies: a
# release is r<commit count>, -dirty when the tree is.
if [[ -z "$VERSION" ]]; then
    VERSION="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null)"
    VERSION="${VERSION:+r$VERSION}"
    VERSION="${VERSION:-dev}"
    if [[ -n "$(git -C "$ROOT" status --porcelain --ignore-submodules=all 2>/dev/null)" ]]; then
        VERSION="$VERSION-dirty"
    fi
fi
distargs=(--keep --version "$VERSION")
[[ -n "$JOBS" ]] && distargs+=(--jobs "$JOBS")
# Every host, named. This used to pass --host only when there was exactly one
# of them and otherwise let dist.sh pick its own default, which was the pair
# of desktop hosts, so the moment a third host existed, asking to publish it
# quietly published the other two instead.
for h in "${HOSTS[@]}"; do distargs+=(--host "$h"); done
if ! TMPDIR="$WORK" RELEASE_FEED_URL="$URL" \
     RELEASE_FEED_KEY="$WORK/feed-key.pem" \
     "$ROOT/dist.sh" "${distargs[@]}"; then
    bad "dist.sh failed, nothing was published"
    exit 1
fi

# A channel lands in three steps, and the order is the point.
sync_channel() {
    local host=$1 doc

    mkdir -p "$DEST/$host" || return 1
    rsync -a --checksum "$WORK/chan-$host/files/" "$DEST/$host/files/" \
        || return 1
    for doc in update_feed.txt update_feed.sig256 \
               main_feed.txt main_feed.sig256; do
        rsync -a --ignore-times "$WORK/chan-$host/$doc" \
              "$DEST/$host/$doc" || return 1
    done
    rsync -a --checksum --delete "$WORK/chan-$host/files/" \
          "$DEST/$host/files/" || return 1
}

for host in "${HOSTS[@]}"; do
    if [[ "$host" == android ]]; then
        # The ANDROID channel is an install of one file. The desktop
        # channels describe a folder the launcher rewrites file by file; an
        # app cannot do that to itself, because on Android the package
        # installer owns the APK. So this channel carries the one file,
        # signed and hashed like any other, and the app's front door fetches
        # exactly that file when the revision here is ahead of its own,
        # proves it against this inventory, and hands it to the installer
        #. Same documents, same
        # signature, same layout as the other two, so nothing downstream
        # needs a second shape to read, and the .apk must be the only
        # entry with that suffix, because the app takes the first.
        apk=("$ROOT"/build/dist/openmmo-"$VERSION"-android-*.apk)
        if [[ ! -f "${apk[0]}" ]]; then
            bad "no android APK for $VERSION in $ROOT/build/dist"
            exit 1
        fi
        rm -rf "$WORK/inst-android"
        mkdir -p "$WORK/inst-android" || exit 1
        # Under the stable name the download page also uses. Nobody sees this
        # one: the app fetches it by its .apk suffix into a private file, and
        # the revision rides in the channel's own main_feed.txt.
        cp -f "${apk[0]}" "$WORK/inst-android/openmmo-android.apk" || exit 1
        # The revision, which feedgen requires and package.sh writes for the
        # hosts that go through it. This host does not, an APK is already
        # the package, so the same line is written here, from the same
        # commit count the release is named by. Without it feedgen refuses,
        # and it is right to: a channel with no revision asks every client
        # for a full update on every check.
        revcount="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null)"
        if [[ -z "$revcount" ]]; then
            bad "cannot count commits for the android channel's revision.txt"
            exit 1
        fi
        printf '%s\n' "$revcount" > "$WORK/inst-android/revision.txt" || exit 1
        say "signing the android channel"
        if ! "$ROOT/feedgen.sh" "$WORK/inst-android" "$KEY" \
                "$WORK/chan-android" --min-revision "$MINREV" > /dev/null; then
            bad "feedgen failed for android"
            exit 1
        fi
        sync_channel android || exit 1
        continue
    fi

    staged=("$WORK"/openmmo-dist-*/pkg-"$host"/openmmo/)
    if [[ ! -d "${staged[0]}" ]]; then
        bad "no staged $host install under $WORK, dist.sh did not leave one"
        exit 1
    fi
    say "signing the $host channel"
    if ! "$ROOT/feedgen.sh" "${staged[0]%/}" "$KEY" "$WORK/chan-$host" \
            --min-revision "$MINREV" > /dev/null; then
        bad "feedgen failed for $host"
        exit 1
    fi
    sync_channel "$host" || exit 1
done

# The zips are the first install; everything after arrives through the feed.
# One name per host, and the name never changes.
stable_name() {  # stable_name <host> -> openmmo-<host>.<ext>
    case $1 in
        android) printf 'openmmo-android.apk';;
        *)       printf 'openmmo-%s.zip' "$1";;
    esac
}
for stale in "$DEST"/openmmo-*; do
    [[ -e "$stale" ]] || continue
    case "$(basename "$stale")" in
        openmmo-windows.zip|openmmo-linux.zip|openmmo-android.apk|\
        openmmo-windows.sha256|openmmo-linux.sha256|openmmo-android.sha256) continue;;
    esac
    rm -f "$stale"
done
for host in "${HOSTS[@]}"; do
    src=("$ROOT"/build/dist/openmmo-"$VERSION"-"$host"-*.zip \
         "$ROOT"/build/dist/openmmo-"$VERSION"-"$host"-*.apk)
    got=''
    for f in "${src[@]}"; do [[ -e "$f" ]] && { got="$f"; break; }; done
    if [[ -z "$got" ]]; then
        bad "the $VERSION $host download is not in $ROOT/build/dist"
        exit 1
    fi
    name=$(stable_name "$host")
    cp -f "$got" "$DEST/$name.new" \
        || { bad "could not copy $(basename "$got") into $DEST"; exit 1; }
    mv -f "$DEST/$name.new" "$DEST/$name" || exit 1
    # The digest of the file that is in the folder, named as it is there, so
    # `sha256sum -c openmmo-<host>.sha256` beside it agrees.
    ( cd "$DEST" && sha256sum "$name" ) > "$DEST/${name%.*}.sha256" \
        || { bad "could not write the $host digest"; exit 1; }
done
cp -f "$WORK/feed-key.pem" "$DEST/feed-key.pem"

rev=$(sed -n 's/.*<revision>\([0-9]*\)<.*/\1/p' "$WORK/chan-${HOSTS[0]}/main_feed.txt")
# Last, and from DEST rather than from the build: the page offers the files
# that are in the folder, so it cannot advertise a download this run did not
# manage to copy.
if ! "$ROOT/downloadpage.sh" "$DEST" "$VERSION" "$rev"; then
    bad "the downloads are published but the page was not written"
    exit 1
fi
say "revision $rev is live in $DEST"

# ------------------------------------------------------------- the read-back
checker=""
if [[ " ${HOSTS[*]} " == *" linux "* ]]; then
    staged=("$WORK"/openmmo-dist-*/pkg-linux/openmmo/bin/openmmo-launch)
    [[ -x "${staged[0]}" ]] && checker="${staged[0]}"
fi
if [[ -z "$checker" && -x "$ROOT/build-release/openmmo-launch" ]]; then
    checker="$ROOT/build-release/openmmo-launch"
fi
readback=0
if [[ -z "$checker" ]]; then
    warn "no linux launcher to read the channels back with (build the linux"
    warn "host in this publish, or make -C mmo launcher RELEASE=1), skipped"
else
    mkdir -p "$WORK/check"
    printf 'probe\n' > "$WORK/check/rom.probe"
    for host in "${HOSTS[@]}"; do
        want=$(sed -n 's/.*<revision>\([0-9]*\)<.*/\1/p' "$WORK/chan-$host/main_feed.txt")
        into="$WORK/check/$host"
        rm -rf "$into"
        mkdir -p "$into" || exit 1
        printf 'rom %s\nfeed %s/feed\nfeed-key %s/feed-key.pem\nfeed-url %s/%s\n' \
            "$WORK/check/rom.probe" "$into" "$WORK" "$URL" "$host" \
            > "$WORK/check/$host.cfg"
        say "fetching $URL/$host back into a throwaway install"
        OPENMMO_ROOT="$into" "$checker" --config "$WORK/check/$host.cfg" \
            --update > "$WORK/check/$host.log" 2>&1
        rc=$?
        # The verdict is the last line: an update says what it is downloading
        # as it goes, and only then what it did. The revision is read out of
        # it only when the update itself succeeded, so no failure that happens
        # to name a number is mistaken for an answer.
        line=$(tail -1 "$WORK/check/$host.log")
        got=''
        [[ $rc -eq 0 ]] && got=$(printf '%s\n' "$line" |
                                 sed -n 's/.*revision \([0-9]*\).*/\1/p')
        if [[ -n "$got" && "$got" == "$want" ]]; then
            if OPENMMO_ROOT="$into" "$checker" --config "$WORK/check/$host.cfg" \
                   --check-feed >> "$WORK/check/$host.log" 2>&1; then
                say "$URL/$host serves r$got whole: it installed, and the gate"
                say "a Play applies accepts what it installed"
            else
                bad "$URL/$host serves r$got, but the install it makes is not"
                bad "one this launcher will start:"
                bad "  $(tail -1 "$WORK/check/$host.log" | sed 's/^openmmo-launch: //')"
                readback=1
            fi
        elif [[ -n "$got" ]]; then
            bad "$URL/$host still answers r$got, not the r$want just written:"
            bad "something between here and the player is caching the channel"
            bad "documents. Purge $URL/$host/main_feed.txt, update_feed.txt and"
            bad "their .sig256 at the proxy, then ask again"
            readback=1
        elif [[ "$line" == *"speaks plain http"* ]]; then
            warn "the launcher at $checker predates https and cannot ask --"
            warn "build the linux host in this publish; the read-back is skipped"
            break
        else
            bad "$URL/$host does not answer as published:"
            bad "  ${line#openmmo-launch: }"
            readback=1
        fi
        # It has said everything it is going to, and the next host wants the
        # room: a filled install is the whole release over again on disk.
        rm -rf "$into"
    done
fi

# The PATH A launcher from before 2026-09-05 still dials.
if [[ "$URL" == https://* ]] && command -v curl > /dev/null; then
    plain="http://${URL#https://}/${HOSTS[0]}/main_feed.txt"
    code=$(curl -sS -o /dev/null -w '%{http_code}' --max-redirs 0 --max-time 20 \
                "$plain" 2>/dev/null || true)
    case "$code" in
        200) say "plain http still answers on the update path, so a launcher"
             say "from before 2026-09-05 (http only, no redirects) can still"
             say "update itself to this release; close it once none is left";;
        301|302|303|307|308)
             warn "plain http on the update path redirects (HTTP $code): a"
             warn "launcher from before 2026-09-05 refuses a redirect and will"
             warn "not update itself. Expected once every install is newer than"
             warn "that; until then, exempt the path from the forced https";;
        *)   warn "plain http on the update path answers ${code:-nothing}: a"
             warn "launcher from before 2026-09-05 cannot update through it";;
    esac
fi

say "serve that folder at $URL, players download a zip once and are"
say "current on every Play after; nothing to configure on their side"
for h in "${HOSTS[@]}"; do
    [[ "$h" == android ]] || continue
    say "the android build is served as an APK a player installs once; every"
    say "start after asks $URL/android which revision is current, and a newer"
    say "one is fetched, proved and handed to the package installer in-app."
done
exit $readback
