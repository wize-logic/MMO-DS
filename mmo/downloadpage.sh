#!/usr/bin/env bash
# mmo/downloadpage.sh, the page a player lands on, written from what a
# publish actually put in the folder.
#
#   mmo/downloadpage.sh DEST_DIR VERSION REVISION
set -uo pipefail

[ $# -eq 3 ] || { printf 'usage: downloadpage.sh DEST_DIR VERSION REVISION\n' >&2; exit 2; }
DEST=$1; VERSION=$2; REVISION=$3

[ -d "$DEST" ] || { printf 'downloadpage: no folder at %s\n' "$DEST" >&2; exit 1; }

esc() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

# The three marks, inline because an <img> would be one more file to publish
# and one more thing to 404. img-src allows 'self' and data:, and an inline
# <svg> is markup rather than an image, so the CSP that drops a <style> has no
# quarrel with these. No "$" anywhere in them, they go through the unquoted
# heredoc below.
read -r -d '' SVG_WINDOWS <<'SVG'
<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="#00A4EF" d="M3 5.7l7.6-1v7.1H3zM11.6 4.5L21 3.2v8.6h-9.4zM3 12.8h7.6v7.1l-7.6-1zM11.6 12.8H21v8.6l-9.4-1.3z"/></svg>
SVG
read -r -d '' SVG_LINUX <<'SVG'
<svg viewBox="0 0 24 24" aria-hidden="true"><ellipse cx="12" cy="13.6" rx="6.1" ry="7" fill="#1b1b1b"/><ellipse cx="12" cy="15" rx="4.1" ry="5.2" fill="#f7f7f7"/><circle cx="12" cy="6.6" r="4.1" fill="#1b1b1b"/><circle cx="10.4" cy="6.3" r="1.2" fill="#fff"/><circle cx="13.6" cy="6.3" r="1.2" fill="#fff"/><circle cx="10.6" cy="6.4" r=".52" fill="#1b1b1b"/><circle cx="13.4" cy="6.4" r=".52" fill="#1b1b1b"/><ellipse cx="12" cy="8.5" rx="1.7" ry="1.1" fill="#f7b733"/><path d="M8.6 19.9c-1 .7-2.4.7-2.8.1-.3-.5.5-1.3 1.7-2zM15.4 19.9c1 .7 2.4.7 2.8.1.3-.5-.5-1.3-1.7-2z" fill="#f7b733"/></svg>
SVG
read -r -d '' SVG_ANDROID <<'SVG'
<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="#3DDC84"><path d="M6.2 10.2h11.6v7.2a2 2 0 0 1-2 2H8.2a2 2 0 0 1-2-2z"/><rect x="2.9" y="10.2" width="2.2" height="6.6" rx="1.1"/><rect x="18.9" y="10.2" width="2.2" height="6.6" rx="1.1"/><path d="M7.3 8.9C7.7 6.7 9.6 5.1 12 5.1s4.3 1.6 4.7 3.8z"/></g><circle cx="9.9" cy="7.3" r=".55" fill="#fff"/><circle cx="14.1" cy="7.3" r=".55" fill="#fff"/><g stroke="#3DDC84" stroke-width="1.1" stroke-linecap="round"><path d="M8.7 3.7l.9 1.5"/><path d="M15.3 3.7l-.9 1.5"/></g></svg>
SVG

# What this build is called where a person can read it.
label() {
    case $1 in
        openmmo-linux.*)    printf 'Linux' ;;
        openmmo-windows.*)  printf 'Windows' ;;
        openmmo-android.*)  printf 'Android' ;;
        *)                  printf '%s' "$1" ;;
    esac
}

# The names carry no REVISION.
found=0
apk=0
desktop=0
for name in openmmo-windows.zip openmmo-linux.zip openmmo-android.apk; do
    f="$DEST/$name"
    [ -e "$f" ] || continue
    case $name in
        *.apk)    apk=1;;
        *)        desktop=1;;
    esac
    # The archive's own hash, taken from the file that is in the folder. The
    # .sha256 beside a desktop zip is package.sh's manifest of the unpacked
    # tree, not the zip's digest, and this used to read a sidecar under a
    # name nothing wrote, so the column stayed empty on every release.
    sha=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
    size=$(( ( $(stat -c %s "$f" 2>/dev/null || stat -f %z "$f") + 1048575 ) / 1048576 ))
    href=$(printf '%s?%s' "$name" "$sha" | esc)
    os=$(label "$name" | esc)
    case $name in
        *-windows.*) mark=$SVG_WINDOWS; slot=win ;;
        *-linux.*)   mark=$SVG_LINUX;   slot=lin ;;
        *-android.*) mark=$SVG_ANDROID; slot=dro ;;
        *)           mark='';           slot=oth ;;
    esac
    row="      <a class=\"dl-row\" href=\"$href\">
        <span class=\"dl-mark\">$mark</span>
        <span class=\"dl-name\">$os<small>${size} MB</small></span>
        <span class=\"dl-go\">Download</span>
      </a>
"
    sum="      <div class=\"sha\"><b>$(printf '%s' "$name" | esc)</b>$(printf '%s' "$sha" | esc)</div>
"
    case $slot in
        win) row_win=$row; sum_win=$sum ;;
        lin) row_lin=$row; sum_lin=$sum ;;
        dro) row_dro=$row; sum_dro=$sum ;;
        *)   row_oth="${row_oth:-}$row"; sum_oth="${sum_oth:-}$sum" ;;
    esac
    found=$((found + 1))
done
[ "$found" -gt 0 ] || {
    printf 'downloadpage: no openmmo-windows.zip, openmmo-linux.zip or openmmo-android.apk in %s\n' "$DEST" >&2
    exit 1
}

# Windows, then Linux, then Android: most players to fewest, rather than
# whatever order the glob happened to hand back.
rows="${row_win:-}${row_lin:-}${row_dro:-}${row_oth:-}"
sums="${sum_win:-}${sum_lin:-}${sum_dro:-}${sum_oth:-}"

# Two update promises, each the truth of its host: the desktop launcher
# rewrites its install every Play; the app fetches the new APK out of the same
# signed channel and asks the phone's package installer to put it in place,
# which is a confirmation the player taps (mmo/FEED.md, "the android channel").
note=''
[ "$desktop" = 1 ] && note='Unzip it anywhere and run it. Nothing to set up, and it
updates itself every time you press Play.'
[ "$apk" = 1 ] && note="$note"' The Android build installs like any other app and
offers each newer build from its own front door, one tap to install.'

cat > "$DEST/download.html" <<HTML
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Download</title>
<link rel="canonical" href="https://mmods.net/download.html">
<link rel="icon" href="/favicon.svg?v=4" type="image/svg+xml">
<link rel="icon" href="/favicon-32.png?v=4" sizes="32x32" type="image/png">
<link rel="icon" href="/favicon-64.png?v=4" sizes="64x64" type="image/png">
<link rel="icon" href="/favicon.ico?v=3" sizes="48x48">
<link rel="apple-touch-icon" href="/apple-touch-icon.png?v=4">
<meta name="theme-color" content="#73ace2">
<link rel="stylesheet" href="/style.css?v=10">
</head>
<body>
<div class="sky" aria-hidden="true"><div></div><div></div><div></div></div>
<header class="bar">
  <a class="brand" href="/"><span class="mark"></span>MMO-DS</a>
  <nav>
    <a href="/">Home</a>
    <a href="/news.html">News</a>
    <a href="/download.html">Download</a>
    <a class="cta" href="/register.html">Play now</a>
  </nav>
</header>

<main class="shell narrow">
  <div class="section-head">
    <h1>Download</h1>
    <p>Sinnoh, start to finish, on your own or with everyone else.</p>
  </div>

  <div class="dl-list">
$rows  </div>

  <p class="dl-note">$note</p>

  <details class="dl-sums">
    <summary>Checksums</summary>
$sums  </details>

  <div class="stack">
    <div class="card">
      <h2>What you need</h2>
      <p>Your own cartridges. The launcher reads three of them and will not start
      without all three: <strong>Platinum</strong>, <strong>HeartGold</strong>
      (SoulSilver works too) and <strong>Black</strong>. No game files are given
      out here or included in the download.</p>
    </div>

    <div class="card">
      <h2>Play offline, or online, or both</h2>
      <p>Pick <strong>Play offline</strong> and the whole game is yours with no
      account and nothing else running. Sign in whenever you like and bring that
      save with you &mdash; party, boxes, badges, Pokedex, key items and money all
      come across, and you can take a session back out again.</p>
      <p>Consumables, TMs, mail, Frontier points and Underground
      goods stay behind. Anything that arrives from an offline save is marked: it
      plays and battles like any other Pokemon, but it cannot be traded, listed on
      the GTL, or entered in a ranked queue, so the market runs on what was earned
      on the server's own dice.</p>
    </div>
  </div>
</main>

<footer class="foot">
  <div class="footer-legal">
    <span class="stamp">Version $(printf '%s' "$VERSION" | esc), revision $(printf '%s' "$REVISION" | esc).</span><br>
    Not affiliated with Nintendo or The Pokemon Company. No ROMs or copyrighted
    assets are distributed.
  </div>
</footer>
</body>
</html>
HTML

printf 'downloadpage: %s/download.html, %s download(s), revision %s\n' \
    "$DEST" "$found" "$REVISION"
