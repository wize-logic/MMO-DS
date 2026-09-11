#!/usr/bin/env bash
# The app on the Android emulator, one device class at a time.
#
#   ./mmo/android/emu.sh check                  # can this host run it at all
#   ./mmo/android/emu.sh list                   # the configurations (emu.conf)
#   ./mmo/android/emu.sh run phone-720 [secs]   # boot one, bench the app, report
#   ./mmo/android/emu.sh sweep [names...]       # every configuration, one table
#   ./mmo/android/emu.sh shell phone-720        # boot one and leave it up
#   ./mmo/android/emu.sh stop                   # kill whatever is running
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT/mmo/android"
SDK="${SDK:-$HOME/.local/opt/android-sdk}"
EMU="$SDK/emulator/emulator"
ADB="$SDK/platform-tools/adb"
AVDMGR="$SDK/cmdline-tools/latest/bin/avdmanager"
CONF="${EMU_CONF:-$HERE/emu.conf}"
OUT="${EMU_OUT:-$ROOT/mmo/build/emu}"
APK="${APK:-$ROOT/mmo/build/android-x86/openmmo.apk}"
ROM="${ROM:-${PC_ROM:-$ROOT/engine/pokeplatinum/build/rom/pokeplatinum.us.nds}}"
PKG=org.openmmo.handheld
ACT=org.openmmo.handheld/.OpenMMOActivity
# An even console port well away from the default 5554, so a developer's own
# emulator is not the one this script kills.
PORT="${EMU_PORT:-5580}"
SERIAL="emulator-$PORT"
# How long the app runs once started. The engine prints a pc-pace block every
# 300 frames and the window a view-frames line every five seconds; the first
# fifteen seconds are boot and are discarded by the report.
SECS="${EMU_SECS:-75}"

say() { printf 'emu: %s\n' "$*" >&2; }
die() { say "$*"; exit 1; }

need() {
    [ -x "$EMU" ] || die "no emulator at $EMU (sdkmanager emulator)"
    [ -x "$ADB" ] || die "no adb at $ADB (sdkmanager platform-tools)"
    [ -x "$AVDMGR" ] || die "no avdmanager at $AVDMGR"
}

# name image cores ram w h dpi gpu, from emu.conf; comments and blanks skipped.
cfg_line() {
    awk -v n="$1" '$1 == n { print; exit }' "$CONF"
}

cmd_list() {
    printf '%-14s %-44s %5s %6s %5s %5s %4s %s\n' name image cores ram w h dpi gpu
    grep -v '^#' "$CONF" | grep -v '^[[:space:]]*$' |
        awk '{ printf "%-14s %-44s %5s %6s %5s %5s %4s %s\n", $1, $2, $3, $4, $5, $6, $7, $8 }'
}

cmd_check() {
    need
    say "emulator: $("$EMU" -version 2>/dev/null | head -1)"
    "$EMU" -accel-check 2>&1 | sed 's/^/emu:   /' >&2
    [ -r /dev/kvm ] && [ -w /dev/kvm ] || die "/dev/kvm is not writable by $(id -un); add the user to kvm"
    say "images:"
    ls -d "$SDK"/system-images/*/*/* 2>/dev/null | sed "s|$SDK/system-images/|emu:   |" >&2
    [ -f "$ROM" ] && say "cartridge: $ROM" || say "no cartridge at $ROM (set ROM=)"
    [ -f "$APK" ] && say "apk: $APK" || say "no x86 apk at $APK (make -f mmo/Makefile.android ANDROID_ABI=x86 apk)"
}

# The AVD, made or remade to the configuration's exact hardware. avdmanager
# writes a config.ini for a generic device; the panel, the cores and the
# memory are rewritten below, which is what the emulator reads at boot.
# Remade every time: an AVD carries a data image, and a run that inherited
# the last run's cartridge, env and saved settings would be measuring that.
make_avd() {
    local name=$1 image=$2 cores=$3 ram=$4 w=$5 h=$6 dpi=$7 gpu=$8
    local avd="openmmo-$name" dir="$HOME/.android/avd/openmmo-$name.avd"

    [ -d "$SDK/system-images/${image#system-images;}" ] 2>/dev/null ||
        [ -d "$SDK/system-images/$(echo "${image#system-images;}" | tr ';' '/')" ] ||
        die "system image $image is not installed (sdkmanager \"$image\")"
    rm -rf "$dir" "$HOME/.android/avd/openmmo-$name.ini"
    echo no | "$AVDMGR" create avd -n "$avd" -k "$image" -d pixel -f >/dev/null 2>&1 ||
        die "avdmanager could not create $avd"
    # Strip the generic device's panel and write the configuration's.
    sed -i -e '/^hw\.lcd\./d' -e '/^hw\.cpu\.ncore/d' -e '/^hw\.ramSize/d' \
           -e '/^skin\./d' -e '/^hw\.gpu\./d' -e '/^hw\.keyboard/d' \
           -e '/^disk\.dataPartition\.size/d' -e '/^hw\.initialOrientation/d' \
           "$dir/config.ini"
    cat >> "$dir/config.ini" <<EOF
hw.lcd.width=$w
hw.lcd.height=$h
hw.lcd.density=$dpi
skin.name=${w}x${h}
skin.path=_no_skin
hw.cpu.ncore=$cores
hw.ramSize=$ram
hw.gpu.enabled=yes
hw.gpu.mode=$gpu
hw.keyboard=yes
hw.initialOrientation=$([ "$w" -ge "$h" ] && echo landscape || echo portrait)
disk.dataPartition.size=4G
EOF
    echo "$avd"
}

boot() {
    local avd=$1 cores=$2 ram=$3 gpu=$4 log=$5
    local i

    cmd_stop
    # -no-window: there is no display to draw on and none is needed, the GL
    # surface renders offscreen and screencap reads it back. -no-snapshot:
    # a cold boot every run, so the run is the run. -no-metrics, because
    # this is a test rig and not a survey respondent.
    ANDROID_AVD_HOME="$HOME/.android/avd" \
    "$EMU" -avd "$avd" -port "$PORT" -no-window -no-audio -no-boot-anim \
        -no-snapshot -no-metrics -gpu "$gpu" -cores "$cores" -memory "$ram" \
        > "$log" 2>&1 &
    echo $! > "$OUT/emulator.pid"
    say "booting $avd on $SERIAL (cores $cores, ram $ram, gpu $gpu)"
    "$ADB" -s "$SERIAL" wait-for-device 2>/dev/null &
    local w=$!
    for i in $(seq 1 90); do
        if ! kill -0 "$(cat "$OUT/emulator.pid")" 2>/dev/null; then
            kill $w 2>/dev/null || true
            die "the emulator exited; $log says why"
        fi
        if ! kill -0 $w 2>/dev/null; then break; fi
        sleep 1
    done
    kill $w 2>/dev/null || true
    for i in $(seq 1 180); do
        if [ "$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then
            say "booted in about $i s"
            return 0
        fi
        if ! kill -0 "$(cat "$OUT/emulator.pid")" 2>/dev/null; then
            die "the emulator exited during boot; $log says why"
        fi
        sleep 1
    done
    die "no sys.boot_completed after 180 s"
}

cmd_stop() {
    if [ -f "$OUT/emulator.pid" ] && kill -0 "$(cat "$OUT/emulator.pid")" 2>/dev/null; then
        "$ADB" -s "$SERIAL" emu kill >/dev/null 2>&1 || true
        sleep 2
        kill "$(cat "$OUT/emulator.pid")" 2>/dev/null || true
        sleep 1
    fi
    rm -f "$OUT/emulator.pid"
}

# The app, the cartridge and the bench's environment onto a booted emulator.
# The emulator's images are rootable, which is what lets a shell write into
# the app's own external directory; a device needs the dance rg556-device-rig
# describes instead.
stage() {
    local dir=$1
    local A="$ADB -s $SERIAL"

    $A root >/dev/null 2>&1 || true
    sleep 2
    $A wait-for-device
    # The image must run 32-bit x86 apps, and not every x86_64 one does: the
    # API 34 google_apis image answers "x86_64,arm64-v8a" and refuses this
    # APK with INSTALL_FAILED_NO_MATCHING_ABIS. The abilist is printed before
    # the install so the refusal reads as what it is.
    local abis out
    abis=$($A shell getprop ro.product.cpu.abilist | tr -d '\r')
    say "abilist $abis"
    case ",$abis," in *,x86,*) ;; *)
        die "this image runs no 32-bit x86 app (abilist $abis); pick an image that lists x86" ;;
    esac
    say "installing $(basename "$APK")"
    if ! out=$($A install -r -g "$APK" 2>&1); then
        die "install failed: $(printf '%s' "$out" | tail -1)"
    fi
    # Launch once so the framework makes the app's directories, then stop it.
    $A shell am start -W -n "$ACT" >/dev/null 2>&1 || true
    sleep 3
    $A shell am force-stop "$PKG" || true
    # The push goes through the lower FILESYSTEM and has to be relabelled.
    # On the API 30 image a plain shell cannot see Android/data at all, so
    # the push runs as root, and root's view of /sdcard is the raw
    # /data/media, not the fuse mount, so the file lands owned by root with
    # the SELinux label storage_file. The app is untrusted_app with its own
    # categories and access() on that file is refused, which the log reports
    # as "nothing named pokeplatinum.us.nds" while ls shows it plainly. The
    # label and owner of a file the app itself made (its log, from the
    # launch above) are the ones to copy. Measured 2026-09-09, an hour.
    local lower ctx uid
    lower="/data/media/0/Android/data/$PKG/files"
    ctx=$($A shell "ls -Z $lower/openmmo.log 2>/dev/null" | awk '{ print $1 }' | tr -d '\r')
    uid=$($A shell dumpsys package "$PKG" | grep -m1 -o 'userId=[0-9]*' | cut -d= -f2 | tr -d '\r')
    [ -n "$ctx" ] && [ -n "$uid" ] ||
        die "the app left no openmmo.log under $lower to copy a label from"
    say "pushing the cartridge"
    $A push "$ROM" "$lower/pokeplatinum.us.nds" 2>&1 | tail -1 >&2
    cat > "$dir/openmmo.env" <<EOF
OPENMMO_SESSION=0
PC_SAVE=none
PC_TRACE_PACE=1
OPENMMO_VIEW_FRAMES=1
${EMU_ENV:-}
EOF
    $A push "$dir/openmmo.env" "$lower/openmmo.env" >/dev/null 2>&1
    $A shell "chown $uid:ext_data_rw $lower/pokeplatinum.us.nds $lower/openmmo.env; \
              chmod 660 $lower/pokeplatinum.us.nds $lower/openmmo.env; \
              chcon '$ctx' $lower/pokeplatinum.us.nds $lower/openmmo.env"
}

# One configuration: boot, stage, run for $SECS, collect, report one row.
cmd_run() {
    local name=$1 secs=${2:-$SECS}
    local line image cores ram w h dpi gpu avd dir A

    need
    line=$(cfg_line "$name")
    [ -n "$line" ] || die "no configuration named $name in $CONF"
    read -r _ image cores ram w h dpi gpu <<<"$line"
    [ -f "$APK" ] || die "no x86 apk at $APK"
    [ -f "$ROM" ] || die "no cartridge at $ROM"
    dir="$OUT/$name"
    rm -rf "$dir"; mkdir -p "$dir"
    # Whatever ends this run, the emulator does not outlive it.
    trap cmd_stop EXIT
    avd=$(make_avd "$name" "$image" "$cores" "$ram" "$w" "$h" "$dpi" "$gpu")
    boot "$avd" "$cores" "$ram" "$gpu" "$dir/emulator.log"
    A="$ADB -s $SERIAL"
    stage "$dir"
    $A logcat -c || true
    say "running the app for $secs s"
    $A shell am start -n "$ACT" >/dev/null
    sleep "$secs"
    $A exec-out screencap -p > "$dir/shot.png" 2>/dev/null || true
    $A logcat -d -v time openmmo:V openmmo.engine:V AndroidRuntime:E '*:S' \
        > "$dir/app.log" 2>/dev/null || true
    $A shell am force-stop "$PKG" || true
    cmd_stop
    report_row "$name" "$image" "$cores" "$ram" "$w" "$h" "$dpi" "$gpu" "$dir/app.log" \
        | tee -a "$OUT/sweep.tsv"
}

# One TSV row out of a run's log: what the app decided, and how it paced.
report_row() {
    local name=$1 image=$2 cores=$3 ram=$4 w=$5 h=$6 dpi=$7 gpu=$8 log=$9
    python3 - "$name" "${image#system-images;}" "$cores" "$ram" "${w}x${h}" "$dpi" "$gpu" "$log" <<'EOF'
import re, sys
name, image, cores, ram, panel, dpi, gpu, log = sys.argv[1:9]
txt = open(log, errors="replace").read()
facts = ""; why = ""; glname = ""
m = re.search(r"device: (\d+x\d+ px.*?)$", txt, re.M)
if m: facts = m.group(1)
for m in re.finditer(r"device: defaults, (.*)$", txt, re.M): why = m.group(1)
m = re.search(r"device: gl '([^']*)'", txt)
if m: glname = m.group(1)
m = re.search(r"gl ([^,\n]*)", facts)
if m and not glname: glname = m.group(1).strip()
threads = re.search(r"threads (\d+)", why); hd3d = re.search(r"hd3d (\d+)", why); ui = re.search(r"ui (\d+)", why)
# pc-pace blocks: fps, late of paced, skipped; work mean; worst miss
pace = re.findall(r"pc-pace: ([\d.]+) fps over ([\d.]+)s \((\d+) of (\d+) frames late, (\d+) pictures skipped\)", txt)
work = re.findall(r"pc-pace:   every frame  work ([\d.]+) \(max ([\d.]+)\)", txt)
worst = re.findall(r"worst miss ([\d.]+) ms", txt)
vf = re.findall(r"view-frames: (\d+) presents in [\d.]+ s \([\d.]+/s\), (\d+) fresh, (\d+) repeats, (\d+) skips", txt)
# drop the first two blocks (boot) when there are enough
pb = pace[2:] if len(pace) > 3 else pace
wb = work[2:] if len(work) > 3 else work
vb = vf[3:] if len(vf) > 4 else vf
fps = sum(float(p[0]) for p in pb) / len(pb) if pb else 0.0
late = sum(int(p[2]) for p in pb); paced = sum(int(p[3]) for p in pb); skipped = sum(int(p[4]) for p in pb)
wmean = sum(float(x[0]) for x in wb) / len(wb) if wb else 0.0
wmax = max((float(x[1]) for x in wb), default=0.0)
wm = max((float(x) for x in worst), default=0.0)
presents = sum(int(v[0]) for v in vb); repeats = sum(int(v[2]) for v in vb); vskips = sum(int(v[3]) for v in vb)
cal = re.findall(r"calibration: hd3d \d+ for \d+ s.*?-- (.*?) \(cap (\d+)", txt)
calv = ("%s/cap%s" % (cal[-1][0].replace("the cap ", "").replace(" ", "-"), cal[-1][1])) if cal else "-"
crash = "crash" if "FATAL EXCEPTION" in txt or "Fatal signal" in txt else ""
booted = "ok" if pace else ("no-pace" if "android_main: up" in txt else "no-app")
row = [name, image, cores, ram, panel, dpi, gpu,
       threads.group(1) if threads else "?", hd3d.group(1) if hd3d else "?", ui.group(1) if ui else "?",
       glname or "?",
       "%.1f" % fps, "%d/%d" % (late, paced), str(skipped),
       "%.2f" % wmean, "%.2f" % wmax, "%.2f" % wm,
       "%d/%d/%d" % (presents, repeats, vskips), calv, crash or booted]
print("\t".join(row))
EOF
}

header() {
    printf '%s\n' "$(printf 'name\timage\tcores\tram\tpanel\tdpi\tgpu\tthreads\thd3d\tui\tgl\tfps\tlate\tskipped\twork_ms\twork_max\tworst_miss\tpresents/repeats/skips\tcalibration\tstate')"
}

cmd_sweep() {
    local names n
    mkdir -p "$OUT"
    if [ $# -gt 0 ]; then names="$*"; else
        names=$(grep -v '^#' "$CONF" | grep -v '^[[:space:]]*$' | awk '{ print $1 }'); fi
    header > "$OUT/sweep.tsv"
    for n in $names; do
        # A subshell, so one configuration's die does not end the sweep.
        (cmd_run "$n") || say "$n: FAILED"
    done
    say "results: $OUT/sweep.tsv"
    column -t -s "$(printf '\t')" "$OUT/sweep.tsv"
}

cmd_shell() {
    local name=$1 line image cores ram w h dpi gpu avd dir
    need
    line=$(cfg_line "$name"); [ -n "$line" ] || die "no configuration named $name"
    read -r _ image cores ram w h dpi gpu <<<"$line"
    dir="$OUT/$name"; mkdir -p "$dir"
    avd=$(make_avd "$name" "$image" "$cores" "$ram" "$w" "$h" "$dpi" "$gpu")
    boot "$avd" "$cores" "$ram" "$gpu" "$dir/emulator.log"
    stage "$dir"
    say "up: $ADB -s $SERIAL shell; $0 stop when done"
}

mkdir -p "$OUT"
case "${1:-}" in
check) cmd_check ;;
list)  cmd_list ;;
run)   shift; [ $# -ge 1 ] || die "run <name> [secs]"; cmd_run "$@" ;;
sweep) shift; cmd_sweep "$@" ;;
shell) shift; [ $# -ge 1 ] || die "shell <name>"; cmd_shell "$@" ;;
stop)  cmd_stop ;;
*)     sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
