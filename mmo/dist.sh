#!/usr/bin/env bash
# mmo/dist.sh, both releases, as two zips, in one command.
#
#   build/dist/openmmo-<version>-linux-x86_64.zip
#   build/dist/openmmo-<version>-windows-x86.zip
#   mmo/dist.sh                    # build both, package both, check both
#   mmo/dist.sh --host windows     # just the one (repeat it to name a set)
#   mmo/dist.sh --host android     # the handheld build, as an APK
#   mmo/dist.sh --local            # this WSL's address, no update channel
#   mmo/dist.sh --no-build         # package the trees as they already stand
#   mmo/dist.sh --out DIR          # somewhere else (default: mmo/build/dist)
#   mmo/dist.sh --version V        # name them yourself (default: git describe)
#   mmo/dist.sh --jobs N           # make -j (default: as many as there are cpus)
#   mmo/dist.sh --keep             # leave the unpacked copies for a look
#   mmo/dist.sh --loopback         # allow a loopback windows build (see below)
#   mmo/dist.sh --release          # optimized, stripped, the live server, default
#   mmo/dist.sh --debug            # -O1 -g, unstripped, this machine's address
#   SERVER_HOST=play.example.net SERVER_LOGIN_PORT=2106 \
#       SERVER_ROOT_KEY=/path/to/game.public.pem mmo/dist.sh
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOSTS=(linux windows)
HOSTS_CHOSEN=0
OUT="$ROOT/build/dist"
VERSION="${VERSION:-}"
BUILD=1
KEEP=0
LOOPBACK_OK=0
RELEASE=1
LOCAL=0
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        # Repeatable, and the first one replaces the default rather than
        # adding to it: `--host windows` has always meant "windows and
        # nothing else" and still does, while `--host linux --host android`
        # names a set. publish.sh needs the set, it publishes every host it
        # was asked for in one run, and a --host that could only ever carry
        # one value is why it used to pass none and silently get the default.
        --host)     if [[ "$HOSTS_CHOSEN" -eq 0 ]]; then
                        HOSTS=("$2")
                    else
                        HOSTS+=("$2")
                    fi
                    HOSTS_CHOSEN=1; shift 2;;
        --out)      OUT="$2"; shift 2;;
        --version)  VERSION="$2"; shift 2;;
        --jobs|-j)  JOBS="$2"; shift 2;;
        --no-build) BUILD=0; shift;;
        --keep)     KEEP=1; shift;;
        --loopback) LOOPBACK_OK=1; shift;;
        # A release for this machine and nobody else: the address this WSL
        # carries on its default route (the one Windows reaches), this tree's
        # own root key, and NO update channel, so the zip cannot fetch the
        # live build over itself, which is exactly how a local stress build
        # downgraded away on 2026-08-27.
        --local)    LOCAL=1; shift;;
        --release)  RELEASE=1; shift;;
        --debug)    RELEASE=0; shift;;
        -h|--help)  usage; exit 0;;
        *) printf 'dist: unknown option %s (try --help)\n' "$1" >&2; exit 2;;
    esac
done

for h in "${HOSTS[@]}"; do
    case "$h" in
        linux|windows|android) ;;
        *) printf 'dist: --host must be linux, windows or android, not %s\n' \
                  "$h" >&2; exit 2;;
    esac
done

# One version string across both archives, resolved once.
if [[ -z "$VERSION" ]]; then
    if [[ "$RELEASE" -eq 1 ]]; then
        VERSION="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null)"
        VERSION="${VERSION:+r$VERSION}"
    fi
    if [[ -z "$VERSION" ]]; then
        VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null)"
        VERSION="${VERSION:-dev}"
    fi
    if [[ -n "$(git -C "$ROOT" status --porcelain --ignore-submodules=all 2>/dev/null)" ]]; then
        VERSION="$VERSION-dirty"
    fi
fi

say()  { printf '\033[1mdist:\033[0m %s\n' "$1"; }
warn() { printf '\033[33mdist:\033[0m %s\n' "$1"; }
bad()  { printf '\033[31mdist:\033[0m %s\n' "$1" >&2; }

command -v zip   >/dev/null || { bad "no zip, install it (apt install zip)";   exit 1; }
command -v unzip >/dev/null || { bad "no unzip, the check unpacks what it writes"; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/openmmo-dist-XXXXXX")"
cleanup() { [[ "$KEEP" -eq 1 ]] || rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

fails=0
ARCHIVES=()

# ---------------------------------------------------------------- building

# The programs a player is handed, per host. `fused` is the game and wants the
# engine checkout; the other three are ours alone.
SERVER_HOST="${SERVER_HOST:-}"
SERVER_LOGIN_PORT="${SERVER_LOGIN_PORT:-}"
SERVER_GAME_PORT="${SERVER_GAME_PORT:-}"
# Where players play, and whose ServerHello the client will accept. These
# belong to whoever runs the server, so nothing is baked in here: set them
# in the environment and a release pins what you set.
RELEASE_HOST="${RELEASE_HOST:-}"
RELEASE_ROOT_KEY="${RELEASE_ROOT_KEY:-}"
SERVER_ROOT_KEY="${SERVER_ROOT_KEY:-}"
# The update channel a release fetches from, compiled in beside the server
# pin so a player configures nothing. It is the operator's channel, so set
# both or a release ships without one.
RELEASE_FEED_URL="${RELEASE_FEED_URL-}"
RELEASE_FEED_KEY="${RELEASE_FEED_KEY-}"

# --local: no channel at all, whatever the environment or the defaults say.
if [[ "$LOCAL" -eq 1 ]]; then
    RELEASE_FEED_URL=""
    RELEASE_FEED_KEY=""
fi

pin_vars() {
    printf 'SERVER_SETTABLE=0'
    [[ -n "$SERVER_HOST" ]]       && printf ' SERVER_HOST=%s' "$SERVER_HOST"
    [[ -n "$SERVER_LOGIN_PORT" ]] && printf ' SERVER_LOGIN_PORT=%s' "$SERVER_LOGIN_PORT"
    [[ -n "$SERVER_GAME_PORT" ]]  && printf ' SERVER_GAME_PORT=%s' "$SERVER_GAME_PORT"
    [[ -n "$SERVER_ROOT_KEY" ]]   && printf ' SERVER_ROOT_KEY=%s' "$SERVER_ROOT_KEY"
    return 0
}

# The address a build actually pins: this script's environment when it carries
# one, and mmo/Makefile's default when it does not, read from the Makefile
# rather than repeated here, so the two cannot drift apart.
pin_host() {
    if [[ -n "$SERVER_HOST" ]]; then
        printf '%s' "$SERVER_HOST"
    else
        sed -n 's/^SERVER_HOST  *?= *//p' "$ROOT/Makefile" | head -1
    fi
}

# The root key a build pins, and the tree's own, read from the Makefile for
# the same reason pin_host is, so the default cannot be repeated here wrongly.
# Both go through rootkey.sh before they are compared, because either may be
# written as a pem path or as hex and only the bytes decide.
pin_root_key() {
    if [[ -n "$SERVER_ROOT_KEY" ]]; then
        printf '%s' "$SERVER_ROOT_KEY"
    else
        sed -n 's/^SERVER_ROOT_KEY  *?= *//p' "$ROOT/Makefile" | head -1
    fi
}

rootkey_bytes() {  # rootkey_bytes <pem-or-hex>
    [[ -n "$1" ]] || return 1
    "$ROOT/tools/rootkey.sh" "$1" 2>/dev/null
}

# An empty answer is "not known", not "loopback": a guard that fires on a
# reading it failed to take would refuse builds that are fine.
is_loopback() {  # is_loopback <host>
    case "$1" in
        127.*|localhost|localhost.*|::1|0.0.0.0) return 0;;
        *) return 1;;
    esac
}

# What the windows half would dial. With --no-build that is not this run's
# environment at all but the pin already sitting in the tree about to be zipped,
# which is the same broken hand-off by a quieter route.
windows_pin_host() {
    if [[ "$BUILD" -eq 1 ]]; then
        pin_host
    else
        sed -n 's/^#define OPENMMO_PIN_HOST  *"\(.*\)"$/\1/p' \
            "$(build_dir windows)/gen/endpoint_pin.h" 2>/dev/null | head -1
    fi
}

# Whether the BUILD that just ran left the working BUILD'S screens in, read
# out of the pin header it generated rather than assumed from this run's
# RELEASE.
check_dev_features() {  # check_dev_features <host>
    local pin dev
    pin="$(build_dir "$1")/gen/endpoint_pin.h"
    if [[ ! -f "$pin" ]]; then
        bad "the $1 build left no $pin, so what it compiled cannot be checked"
        return 1
    fi
    dev="$(sed -n 's/^#define OPENMMO_PIN_DEV_FEATURES  *\([0-9]*\)$/\1/p' \
           "$pin" | head -1)"
    if [[ "$dev" != 0 ]]; then
        bad "the $1 build compiled DEV_FEATURES=${dev:-<unset>}: this release"
        bad "would carry the working build's screens, the Pokegear button,"
        bad "the Johto & Kanto trainer card and the PC menu's TRAVEL row."
        bad "Its make was not given RELEASE=1 (mmo/dist.sh, build_host)."
        return 1
    fi
    return 0
}

# The address on the interface that carries the default route, the one a
# machine off this host reaches, and under WSL the one Windows reaches. A hint
# for the message below, not a decision: a real release wants a name.
reachable_hint() {
    local dev ip
    dev="$(ip -4 route show default 2>/dev/null | awk '{print $5; exit}')"
    ip="$(ip -4 -o addr show ${dev:+dev "$dev"} scope global 2>/dev/null |
              awk '{split($4,a,"/"); print a[1]; exit}')"
    printf '%s' "${ip:-play.example.net}"
}

# Where a host's programs land. RELEASE=1 is a tree of its own (mmo/Makefile
# says why), so this has to be asked rather than assumed, package.sh's own
# default is the working tree.
build_dir() {  # build_dir <host>
    local d="$ROOT/build"
    # Android names its own tree rather than taking the suffix below, because
    # Makefile.android's BUILD is build/android with the suffix after the host
    # and not before it. It is a release/debug pair like the other two: this
    # said it was not for a while, and the android make was then left without
    # RELEASE to match, which is how a release APK came to carry the working
    # build's Pokegear button (see build_host).
    if [[ "$1" == android ]]; then
        printf '%s' "$d/android$([[ "$RELEASE" -eq 1 ]] && printf -- -release)"
        return
    fi
    [[ "$1" == windows ]] && d="$d/win"
    [[ "$RELEASE" -eq 1 ]] && d="$d-release"
    printf '%s' "$d"
}

build_host() {  # build_host <host>
    local pin feed_url= feed_key=
    read -r -a pin <<<"$(pin_vars)"
    if [[ "$RELEASE" -eq 1 && -n "$RELEASE_FEED_URL" ]]; then
        feed_url="$RELEASE_FEED_URL/$1"
        feed_key="$RELEASE_FEED_KEY"
    fi
    case "$1" in
        linux)   make -C "$ROOT" -j"$JOBS" RELEASE=$RELEASE "${pin[@]}" \
                      FEED_URL="$feed_url" FEED_KEY="$feed_key" \
                      all viewer launcher fused;;
        windows) make -C "$ROOT" -f Makefile.win -j"$JOBS" RELEASE=$RELEASE \
                      "${pin[@]}" FEED_URL="$feed_url" FEED_KEY="$feed_key" \
                      programs fused;;
        # `apk`, not `apk-live`: apk-live exists to read the pin back out of
        # this file for somebody building by hand, and this run already has
        # it. Passing it through twice is how the two would drift.
        #
        # The channel is pinned here for the app'S own updater. The other
        # two hosts unpack into a folder their launcher rewrites file by file;
        # an APK is replaced by the package installer or not at all, so
        # there is no in-place patcher on this host. What the app does with
        # the pin is read the signed document that names the current revision
        # (main_feed.txt, which publish.sh writes for the android channel like
        # any other), compare it with the revision this APK was built at, and
        # if it is behind fetch the one .apk the channel lists, prove it
        # against the signed inventory, and hand it to the installer
        # (mmo/FEED.md, "the android channel"). The update must be signed by
        # the same key as the installed app: the release key, never the
        # debug one (Makefile.android, "the release key").
        #
        # RELEASE, for the same reason the other two get it, and it was
        # missing here. Without it Makefile.android falls back to RELEASE=0
        # and so to DEV_FEATURES=1, and every published APK carried the
        # screens a release is supposed to have none of, the Pokegear
        # button on the HUD bar, the Johto & Kanto trainer card, and the
        # TRAVEL row on the PC menu (mmo/Makefile, DEV_FEATURES). It also
        # compiled the client at the working build's -O1 -g.
        android) make -C "$ROOT" -f Makefile.android -j"$JOBS" \
                      RELEASE=$RELEASE "${pin[@]}" \
                      FEED_URL="$feed_url" FEED_KEY="$feed_key" apk;;
    esac
}

# ---------------------------------------------------------------- checking

# Every file in the folder, against the hashes package.sh wrote beside the
# archive. They are deliberately not in the folder, a list that travels with
# the tree it describes agrees with a truncated copy of itself, and this reads
# the unpacked tree, not the staged one they were taken from.
check_hashes() {  # check_hashes <unpacked dir> <sums file>
    local dir="$1" sums="$2" out
    if [[ ! -f "$sums" ]]; then
        bad "no $(basename "$sums") beside the archive -- nothing to check the tree against"
        return 1
    fi
    out="$(cd "$dir" && grep -E '^[0-9a-f]{64}  ' "$sums" | sha256sum -c --quiet - 2>&1)"
    if [[ -n "$out" ]]; then
        bad "the unpacked tree does not match the hashes beside its archive:"
        printf '%s\n' "$out" >&2
        return 1
    fi
    return 0
}

# What `file` says each program is. The two hosts disagree about everything
# except the answer to "is this 32-bit x86", which is the one thing that is not
# negotiable: the client's structs are the console's.
check_binaries() {  # check_binaries <unpacked dir> <host>
    local dir="$1" host="$2" exe="" want="ELF 32-bit" rc=0 f desc
    [[ "$host" == windows ]] && { exe=".exe"; want="PE32 executable"; }

    for f in pokeplatinum; do
        desc="$(file -b "$dir/bin/$f$exe")"
        case "$desc" in
            *"$want"*80386*|*"$want"*i386*) ;;
            *) bad "bin/$f$exe is not 32-bit x86: $desc"; rc=1;;
        esac
    done
    # The window and the front door are 64-bit ELF on Linux (the host's SDL2 is)
    # and 32-bit pe on Windows, so only their existence and their format are
    # worth asserting here.
    for f in openmmo-view openmmo-launch; do
        desc="$(file -b "$dir/bin/$f$exe")"
        case "$desc" in
            ELF*|PE32*) ;;
            *) bad "bin/$f$exe is not a program: $desc"; rc=1;;
        esac
    done
    if [[ "$host" == windows && ! -f "$dir/bin/SDL2.dll" ]]; then
        bad "no bin/SDL2.dll, the window does not open without it"
        rc=1
    fi
    return $rc
}

# Start the front door the way a person does, from the unpacked folder and
# with nothing else on the machine.
check_runs() {  # check_runs <unpacked dir> <host>
    local dir="$1" host="$2" out rc
    if [[ "$host" == linux ]]; then
        out="$(cd "$dir" && timeout 60 ./openmmo --help 2>&1)"; rc=$?
    elif command -v wine >/dev/null 2>&1; then
        out="$(cd "$dir" && timeout 120 wine bin/openmmo-launch.exe --help 2>&1)"; rc=$?
    else
        warn "  not run: no wine on this machine, so the .exe was read and not started"
        return 0
    fi
    if [[ $rc -ne 0 ]]; then
        bad "the front door exited $rc:"
        printf '%s\n' "$out" | head -5 >&2
        return 1
    fi
    case "$out" in
        *usage:*) return 0;;
        *) bad "the front door started but printed no usage:"; printf '%s\n' "$out" | head -5 >&2; return 1;;
    esac
}

check_archive() {  # check_archive <zip> <host>
    local ar="$1" host="$2" dir rc=0
    dir="$WORK/$host"
    rm -rf "$dir"; mkdir -p "$dir"
    unzip -qq "$ar" -d "$dir" || { bad "the zip does not unpack"; return 1; }

    local top
    top="$(find "$dir" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [[ -d "$top" ]] || { bad "the zip has no folder in it"; return 1; }

    check_hashes "$top" "${ar%.zip}.sha256" || rc=1
    check_binaries "$top" "$host" || rc=1
    # An unpacked front door nobody can run is the failure that reads as "the
    # download is broken". zip keeps the mode bits in the external attributes,
    # which is exactly what this is here to prove is still true.
    local door="openmmo"
    [[ "$host" == windows ]] && door="OpenMMO.cmd"
    if [[ ! -f "$top/$door" ]]; then
        bad "no $door at the top of the folder"; rc=1
    elif [[ "$host" == linux && ! -x "$top/$door" ]]; then
        bad "$door unpacked without its execute bit"; rc=1
    fi
    check_runs "$top" "$host" || rc=1
    return $rc
}

# ------------------------------------------------------------------ android
#
#   * the payload is there and is the right machine. A link that dropped the
#     library still produces an APK that installs.
#   * it is signed. An unsigned APK is refused by the installer with a
#     message about the certificate and nothing about the build.
#   * it dials the address this run pinned. The android build has its own
#     gen/endpoint_pin.h and its own make invocation, and a pin that did not
#     reach the library is the failure with no symptom until a player is at
#     the title screen, the same one the windows guard below exists for.
check_apk() {  # check_apk <apk>
    local apk="$1" rc=0 so="$WORK/apk-lib.so" names
    # The same default Makefile.android's APKSIGNER has, so a tree that can
    # build an APK can also check the signature on one.
    local signer="${APKSIGNER:-$HOME/.local/opt/android-sdk/build-tools/35.0.0/apksigner}"

    names="$(unzip -Z1 "$apk" 2>/dev/null)" || {
        bad "$(basename "$apk") is not readable as a zip -- aapt2 wrote nothing usable"
        return 1
    }
    if ! printf '%s\n' "$names" | grep -qx 'lib/armeabi-v7a/libpokeplatinum.so'; then
        bad "$(basename "$apk") carries no lib/armeabi-v7a/libpokeplatinum.so:"
        bad "the game is missing from the package and the app would install and"
        bad "then die at load."
        rc=1
    else
        rm -f "$so"
        if unzip -p "$apk" lib/armeabi-v7a/libpokeplatinum.so > "$so" 2>/dev/null; then
            local desc; desc="$(file -b "$so")"
            case "$desc" in
                *"ELF 32-bit"*) ;;
                *) bad "the packaged game is '$desc', not a 32-bit ELF"; rc=1;;
            esac
            case "$desc" in
                *ARM*) ;;
                *) bad "the packaged game is '$desc', not ARM"; rc=1;;
            esac
            case "$desc" in
                *"shared object"*) ;;
                *) bad "the packaged game is '$desc', not a shared object --"
                   bad "the framework loads a library, and cannot load this"
                   rc=1;;
            esac
            # Which SERVER these BYTES trust, read out of the library that
            # shipped rather than out of the header the build generated:
            # telling those two apart is the whole reason this reads the APK
            # back instead of trusting gen/endpoint_pin.h.
            #
            # The KEY and NOT the address, because the address is not in there
            # to find. src/endpoint.c stores the host with OBFSTR_BYTES,
            # deliberately not a plain string a `strings` pass can lift out of
            # a shipped binary, while the root key is a plain byte array in
            # src/session.c. The key is also the half with no symptom: a wrong
            # address fails at connect and says so, a wrong key fails at the
            # last line of the handshake with the address, the ports and the
            # login every one of them right. The address is checked against
            # the pin at the top of this run, where it can be.
            local want_key
            want_key="$(rootkey_bytes "$(pin_root_key)")"
            want_key="${want_key//0x/}"; want_key="${want_key//,/}"
            if [[ -n "$want_key" ]]; then
                # A 64-character needle cannot land on an odd nibble by
                # accident, so a flat hex dump is a sound place to look for it.
                if ! xxd -p "$so" | tr -d '\n' | grep -qF "$want_key"; then
                    bad "the packaged game does not carry the root key this run"
                    bad "pinned (${want_key:0:16}...). The pin did not reach the"
                    bad "android link, and every session would end at the last"
                    bad "line of the handshake with nothing else wrong."
                    rc=1
                fi
            fi
            # SIXTEEN KILOBYTE PAGES, checked on the bytes that ship rather
            # than on the flags that built them. Android 15 runs on devices
            # whose page size is 16 KB, and a library whose LOAD segments are
            # aligned to the old 4 KB cannot be mapped on one, the app
            # installs and dies in the linker naming only the library, which
            # reads like a corrupt download. Two things have to hold and each
            # fails alone: the link asks for the alignment (Makefile.android's
            # PAGEALIGN) and the packaging keeps it (stored, never deflated,
            # so the loader can map it out of the APK, which is what
            # extractNativeLibs="false" means, and that is the default from
            # API 30).
            if command -v readelf >/dev/null 2>&1; then
                local aln
                aln="$(readelf -lW "$so" 2>/dev/null \
                       | awk '/LOAD/ { print $NF }' | sort -u)"
                if [[ "$aln" != "0x4000" ]]; then
                    bad "the packaged game's LOAD segments are aligned"
                    bad "'${aln:-unreadable}', not 0x4000. A 16 KB-page device"
                    bad "(Android 15 and later) installs it and then fails to"
                    bad "load it."
                    rc=1
                fi
            fi
            # The method is read and then compared, rather than grepped for
            # with the answer inverted: an unreadable listing gives an empty
            # pipe, and a `grep -q` that finds nothing in one is
            # indistinguishable from a deflated library. Naming the method
            # tells those two apart.
            local method
            method="$(unzip -lv "$apk" lib/armeabi-v7a/libpokeplatinum.so \
                      2>/dev/null | awk '$NF == "lib/armeabi-v7a/libpokeplatinum.so" { print $2 }')"
            case "$method" in
                Stored) ;;
                "") bad "the packaged game's zip entry could not be listed, so"
                    bad "its compression is unknown."
                    rc=1;;
                *)  bad "the packaged game is '$method' in the APK, not Stored."
                    bad "From API 30 the platform maps the library out of the"
                    bad "package instead of unpacking it, and it can only do"
                    bad "that with a stored, page-aligned entry."
                    rc=1;;
            esac
        else
            bad "the packaged game could not be read back out of $(basename "$apk")"
            rc=1
        fi
    fi
    if [[ -x "$signer" ]]; then
        if ! "$signer" verify "$apk" >/dev/null 2>&1; then
            bad "$(basename "$apk") is not signed -- the installer refuses it"
            rc=1
        fi
    elif ! printf '%s\n' "$names" | grep -qE '^META-INF/.*\.(RSA|EC|DSA)$'; then
        # No apksigner to ask, so fall back to the v1 block. A v2-only APK has
        # none, which is why this is a warning and the tool above is not.
        warn "no apksigner at $signer and no v1 signature block: the signature"
        warn "of $(basename "$apk") has NOT been checked"
    fi
    return $rc
}

# ---------------------------------------------------------------- the run

say "openmmo $VERSION -> $OUT"
if [[ "$RELEASE" -eq 1 ]]; then
    say "  release build (our programs at -O2, debug info stripped)"
else
    warn "  debug build (-O1 with debug info, nothing stripped)"
fi
# Which server a zip is a client for, decided once, here. A release goes to the
# live one; a debug build goes to this machine, detected now rather than
# remembered because WSL2 hands out a new address across reboots. Not gated on
# the Makefile's default being loopback for the release half: a release is for
# players whatever this tree happens to be configured to dial.
if [[ "$RELEASE" -eq 1 && "$BUILD" -eq 1 && "$LOCAL" -eq 0 ]]; then
    if [[ -z "$SERVER_HOST$RELEASE_HOST" || -z "$SERVER_ROOT_KEY$RELEASE_ROOT_KEY" ]]; then
        bad "a release needs the server it is for. Set RELEASE_HOST and"
        bad "RELEASE_ROOT_KEY (a pem path or an 04-hex point), or set"
        bad "SERVER_HOST and SERVER_ROOT_KEY to pin one build."
        exit 1
    fi
fi
if [[ "$BUILD" -eq 1 && -z "$SERVER_ROOT_KEY" && "$RELEASE" -eq 1 && "$LOCAL" -eq 0 ]]; then
    SERVER_ROOT_KEY="$RELEASE_ROOT_KEY"
fi
if [[ "$BUILD" -eq 1 && -z "$SERVER_HOST" ]]; then
    if [[ "$LOCAL" -eq 1 ]]; then
        DETECTED="$(reachable_hint)"
        if [[ "$DETECTED" == play.example.net ]]; then
            bad "--local: this machine's address on its default route cannot be"
            bad "read, so there is nothing to pin. Name it: SERVER_HOST=<ip> $0 ..."
            exit 2
        fi
        SERVER_HOST="$DETECTED"
    elif [[ "$RELEASE" -eq 1 ]]; then
        SERVER_HOST="$RELEASE_HOST"
    elif is_loopback "$(pin_host)"; then
        DETECTED="$(reachable_hint)"
        if [[ "$DETECTED" != play.example.net ]]; then
            SERVER_HOST="$DETECTED"
        fi
    fi
fi
# Said out loud because it cannot be changed afterwards: a zip is a client for
# one server, and a release built with the tree's loopback default is a release
# nobody can play.
if [[ "$BUILD" -eq 1 ]]; then
    if [[ "$LOCAL" -eq 1 && "$SERVER_HOST" == "${DETECTED:-}" ]]; then
        say "  server $SERVER_HOST (--local: this machine, no update channel)"
    elif [[ -n "${DETECTED:-}" && "$SERVER_HOST" == "${DETECTED:-}" ]]; then
        say "  server $SERVER_HOST (this machine's current address; it moves"
        say "  across reboots, which is why a release does not use it)"
    elif [[ "$RELEASE" -eq 1 && "$SERVER_HOST" == "$RELEASE_HOST" ]]; then
        say "  server $SERVER_HOST (the live one; compiled in, no setting for it)"
    elif [[ -n "$SERVER_HOST" ]]; then
        say "  server $SERVER_HOST (compiled in, no setting for it)"
    else
        say "  server: mmo/Makefile's SERVER_HOST, set it here for a release"
    fi

    # The other half of the same fact, and the one with no symptom until a
    # player is at the title screen: the key is what decides whether the
    # handshake completes, and a build can dial the right address while
    # trusting the wrong signer.
    pinned="$(rootkey_bytes "$(pin_root_key)")"
    tree="$(rootkey_bytes "$(sed -n 's/^SERVER_ROOT_KEY  *?= *//p' "$ROOT/Makefile" | head -1)")"
    pinhex="${pinned//0x/}"; pinhex="${pinhex//,/}"
    dials="${SERVER_HOST:-$(pin_host)}"
    if [[ -z "$pinned" ]]; then
        bad "the root key this build would pin cannot be read:"
        bad "    $(pin_root_key)"
        bad "mmo/tools/rootkey.sh takes a pem or the uncompressed point as hex."
        exit 2
    elif [[ "$pinned" != "$tree" ]]; then
        if [[ "$RELEASE" -eq 1 && "$SERVER_ROOT_KEY" == "$RELEASE_ROOT_KEY" ]]; then
            say "  trusting ${pinhex:0:16}... (the live server's root key)"
        else
            say "  trusting ${pinhex:0:16}... (compiled in, no setting for it)"
        fi
    elif is_loopback "$dials" || [[ "$dials" == "${DETECTED:-}" ||
                                    "$dials" == "$(reachable_hint)" ]]; then
        # This tree's key and this machine's address: the servers it dials are
        # the ones that generated that key, which is the whole debug case.
        say "  trusting ${pinhex:0:16}... (this tree's own servers)"
    else
        # Not a refusal: it is the one thing here that cannot be checked without
        # asking the server, and a second server of our own, built from this
        # tree, is a real thing to want. Said in full, because the failure it
        # warns about names nothing a player or a log can act on, the client
        # says only that the signature does not verify, while the address, the
        # ports and the login are every one of them right.
        warn "  this build dials $dials but trusts THIS TREE'S key. Unless that"
        warn "  server serves with server.game/src/main/resources/game.private.pem,"
        warn "  every session of it ends at the handshake. Pin that server's key:"
        warn "      SERVER_ROOT_KEY=<its game.public.pem> $0 ..."
    fi
fi

# Refused rather than warned about, because the failure it prevents does not
# look like this decision: the player gets "the target machine actively
# refused it" at the login connect, and SERVER_SETTABLE=0 has already compiled
# out every override that could rescue the zip.
if [[ "$LOOPBACK_OK" -eq 0 ]]; then
    for host in "${HOSTS[@]}"; do
        [[ "$host" == windows ]] || continue
        if is_loopback "$(windows_pin_host)"; then
            if [[ "$HOSTS_CHOSEN" -eq 1 ]]; then
                bad "the windows release would dial $(windows_pin_host), which on Windows is the"
                bad "player's own machine and not this one. Every session would end at the"
                bad "login connect, and a release has no override left to fix it with."
                bad "Pin the address a Windows client can reach:"
                bad "    SERVER_HOST=$(reachable_hint) mmo/dist.sh --host windows"
                bad "--loopback builds it anyway (the servers are on that machine)."
                exit 2
            fi
            warn "windows: skipped -- it would dial $(windows_pin_host), which on Windows"
            warn "is the player's own machine, not this one. For that zip, pin an"
            warn "address a Windows client can reach:"
            warn "    SERVER_HOST=$(reachable_hint) mmo/dist.sh --host windows"
            warn "(--loopback builds it anyway; the linux half continues below)"
            NEXT_HOSTS=()
            for h in "${HOSTS[@]}"; do
                [[ "$h" == windows ]] || NEXT_HOSTS+=("$h")
            done
            HOSTS=("${NEXT_HOSTS[@]}")
        fi
    done
    # The same trap, harder. A loopback pin on Windows is the player's own PC;
    # on a phone it is the phone, which is running nothing at all, and unlike
    # the desktop hosts there is no shell on it to set an override in even if
    # SERVER_SETTABLE had left one. An APK is also the one release here that a
    # player installs rather than unpacks, so a bad one has to be uninstalled
    # rather than deleted. Refused every time, chosen or not.
    for host in "${HOSTS[@]}"; do
        [[ "$host" == android ]] || continue
        if is_loopback "$(pin_host)"; then
            bad "the android release would dial $(pin_host), which on a phone is the"
            bad "phone. Nothing is listening there and the APK has no setting left"
            bad "to point it anywhere else. Pin an address the device can reach:"
            bad "    SERVER_HOST=$(reachable_hint) mmo/dist.sh --host android"
            bad "--loopback builds it anyway."
            exit 2
        fi
    done
fi

mkdir -p "$OUT" || exit 1

# The channel is announced the way the server pin is, and its absence even
# more so: a release with no channel never auto-updates and a .forceupdate
# beside it does nothing, which is invisible until somebody stands at a
# Desktop wondering why.
if [[ "$RELEASE" -eq 1 ]]; then
    if [[ -n "$RELEASE_FEED_URL" ]]; then
        say "updating from $RELEASE_FEED_URL/<host> (the compiled-in channel)"
    elif [[ "$LOCAL" -eq 1 ]]; then
        say "no update channel (--local): these releases never fetch"
    else
        warn "NO update channel: these releases will never auto-update"
    fi
fi

for host in "${HOSTS[@]}"; do
    say "--- $host"
    if [[ "$BUILD" -eq 1 ]]; then
        say "  building"
        if ! build_host "$host" >"$WORK/$host-build.log" 2>&1; then
            bad "the $host build failed:"
            # The lines that say what broke, not the last lines. make runs
            # these goals in parallel, so the tail is usually whichever goal
            # finished last saying it was fine, while the one that failed
            # scrolled past. That has already cost one round trip.
            if ! grep -nE "error:|Error [0-9]|\*\*\*|undefined reference|No such file" \
                    "$WORK/$host-build.log" | head -20 >&2; then
                tail -25 "$WORK/$host-build.log" >&2
            fi
            bad "the whole log: $WORK/$host-build.log"
            fails=$((fails + 1))
            continue
        fi
    fi

    # Outside the build guard on purpose: --no-build zips a tree this run did
    # not compile, which is the quieter half of the same hand-off the windows
    # address guard exists for.
    if [[ "$RELEASE" -eq 1 ]] && ! check_dev_features "$host"; then
        fails=$((fails + 1))
        continue
    fi

    # The APK is already the package. package.sh's whole job is to gather
    # loose programs and resources into an archive with a front door beside
    # them, and aapt2 has done exactly that, with a manifest, a signature
    # and an alignment that a zip rebuilt around it would break. So this host
    # renames and hashes what the build made and checks it in place.
    if [[ "$host" == android ]]; then
        ar="$OUT/openmmo-$VERSION-android-armeabi-v7a.apk"
        if ! cp -f "$(build_dir android)/openmmo.apk" "$ar"; then
            bad "the android build made no openmmo.apk"
            fails=$((fails + 1))
            continue
        fi
        ( cd "$OUT" && sha256sum "$(basename "$ar")" ) > "${ar%.apk}.sha256" || {
            bad "the android hash could not be written"
            fails=$((fails + 1))
            continue
        }
        say "  checking $(basename "$ar")"
        if check_apk "$ar"; then
            say "  ok"
            ARCHIVES+=("$ar")
        else
            fails=$((fails + 1))
        fi
        continue
    fi

    # Staged under the work directory, not under $OUT: both hosts stage a folder
    # of the same name, and a release directory left behind from the other host
    # is exactly the sort of thing that gets handed over by mistake. What lands
    # in $OUT is the two zips and nothing else.
    say "  packaging"
    if ! "$ROOT/package.sh" --host "$host" --archive zip \
                            --build "$(build_dir "$host")" \
                            $([[ "$RELEASE" -eq 1 ]] && printf -- --strip) \
                            --out "$WORK/pkg-$host" --version "$VERSION"; then
        bad "the $host package was not written"
        fails=$((fails + 1))
        continue
    fi

    platform="linux-x86_64"
    [[ "$host" == windows ]] && platform="windows-x86"
    ar="$OUT/openmmo-$VERSION-$platform.zip"
    mv -f "$WORK/pkg-$host/openmmo-$VERSION-$platform.zip" "$ar" || {
        bad "the $host zip could not be moved to $OUT"
        fails=$((fails + 1))
        continue
    }
    # The hashes travel beside the zip, never inside it. Moving one without the
    # other leaves a release nothing can be checked against.
    mv -f "$WORK/pkg-$host/openmmo-$VERSION-$platform.sha256" "${ar%.zip}.sha256" || {
        bad "the $host hashes could not be moved to $OUT"
        fails=$((fails + 1))
        continue
    }

    say "  checking $(basename "$ar")"
    if check_archive "$ar" "$host"; then
        say "  ok"
        ARCHIVES+=("$ar")
    else
        fails=$((fails + 1))
    fi
done

printf '\n'
if [[ "${#ARCHIVES[@]}" -gt 0 ]]; then
    for ar in "${ARCHIVES[@]}"; do
        printf 'dist: %s (%s)\n' "$ar" "$(du -h "$ar" | cut -f1)"
        printf 'dist: %s\n' "${ar%.*}.sha256"
    done
fi
[[ "$KEEP" -eq 1 ]] && say "unpacked copies left in $WORK"

if [[ "$fails" -gt 0 ]]; then
    bad "$fails of ${#HOSTS[@]} release(s) FAILED, do not hand these over"
    exit 1
fi
say "${#ARCHIVES[@]} release(s) written, unpacked and checked."
say "The cartridge image is still the player's to supply; these are local builds."
