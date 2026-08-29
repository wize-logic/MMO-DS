#!/usr/bin/env bash
# Put the APK on a device and watch it, from this checkout.
#
#   ./mmo/android/dev.sh build      # make the APK
#   ./mmo/android/dev.sh install    # build, then install it
#   ./mmo/android/dev.sh run        # install, launch, and follow the log
#   ./mmo/android/dev.sh log        # follow the log of whatever is running
#   ./mmo/android/dev.sh stop       # force-stop the app
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PKG=org.openmmo.handheld
# The one Java class: NativeActivity subclassed to hear the document
# picker's answer. android.app.NativeActivity stopped existing here the day
# the cartridge started arriving through the system picker.
ACT=org.openmmo.handheld.OpenMMOActivity
MK="$ROOT/mmo/Makefile.android"
APK="${APK:-$ROOT/mmo/build/android/openmmo.apk}"

ADB="${ADB:-}"
if [ -z "$ADB" ]; then
    if command -v adb >/dev/null 2>&1; then
        ADB=adb
    elif [ -x /mnt/c/Users/"$USER"/platform-tools/adb.exe ]; then
        ADB=/mnt/c/Users/"$USER"/platform-tools/adb.exe
    elif [ -x /mnt/c/Users/$WINUSER/platform-tools/adb.exe ]; then
        ADB=/mnt/c/Users/$WINUSER/platform-tools/adb.exe
    else
        echo "dev.sh: no adb; set ADB=" >&2; exit 1
    fi
fi

# A Windows adb needs a Windows path. Stage, then say where it landed.
stage_apk() {
    case "$ADB" in
    *.exe)
        local dir=/mnt/c/Users/$WINUSER/rg556-stage
        mkdir -p "$dir"
        cp -f "$APK" "$dir/openmmo.apk"
        printf '%s' 'C:\Users\<you>\rg556-stage\openmmo.apk'
        ;;
    *)  printf '%s' "$APK" ;;
    esac
}

# The playable TARGET, not `apk`.
cmd_build() { make -f "$MK" "${APK_TARGET:-apk-live}"; }

cmd_install() {
    cmd_build
    local path; path="$(stage_apk)"
    echo "installing $path"
    "$ADB" install -r -d "$path" || {
        echo "reinstall failed; removing and trying once more" >&2
        "$ADB" uninstall "$PKG" >/dev/null 2>&1 || true
        "$ADB" install "$path"
    }
}

cmd_stop() { "$ADB" shell am force-stop "$PKG"; }

cmd_run() {
    cmd_install
    "$ADB" shell am force-stop "$PKG" || true
    "$ADB" logcat -c || true
    "$ADB" shell am start -n "$PKG/$ACT"
    cmd_log
}

cmd_log() {
    "$ADB" logcat -v time openmmo:V AndroidRuntime:E ActivityManager:I \
        DEBUG:V libc:E "*:S"
}

case "${1:-run}" in
build)   cmd_build ;;
install) cmd_install ;;
run)     cmd_run ;;
log)     cmd_log ;;
stop)    cmd_stop ;;
*) echo "usage: dev.sh {build|install|run|log|stop}" >&2; exit 2 ;;
esac
