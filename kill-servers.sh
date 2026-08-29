#!/bin/sh
# Find and stop every OpenMMO server on this machine.
#
#   ./kill-servers.sh              # stop them
#   ./kill-servers.sh -n           # list what would be stopped, signal nothing
#   * the gradle and kotlin daemons, which are java too. They exist between
#     builds on purpose and killing one only buys a slow recompile.
#   * the database containers. They hold the accounts and starting them again
#     is the slow part; `./stop-server.sh --db` is where that lives.
#   * every process that is not a JVM, which is the client, the viewer, the
#     launcher and anything else that happens to be running.
set -eu

DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        -n|--dry-run) DRY=1; shift ;;
        -h|--help)
            sed -n '2,/^set -eu$/{/^set -eu$/d; s/^# \{0,1\}//; p;}' "$0"
            exit 0 ;;
        *) printf 'kill-servers: unknown option %s (try --help)\n' "$1" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1m[openmmo]\033[0m %s\n' "$1"; }
warn() { printf '\033[33m[openmmo]\033[0m %s\n' "$1"; }

# What a pid is, or nothing at all. Prints a human name for the thing on
# success; the caller treats a non-zero return as "not ours".
classify() {
    _pid=$1
    _comm=$(cat "/proc/$_pid/comm" 2>/dev/null) || return 1
    [ "$_comm" = java ] || return 1
    _cmd=$(tr '\0' '\n' < "/proc/$_pid/cmdline" 2>/dev/null) || return 1

    # Spared first, so that a daemon is spared even if a task name has found
    # its way into its arguments. A daemon is never the thing holding a port.
    case "$_cmd" in
        *org.gradle.launcher.daemon.bootstrap.GradleDaemon*) return 1 ;;
        *org.jetbrains.kotlin.daemon.KotlinCompileDaemon*)   return 1 ;;
    esac

    # The server itself. Gradle's `run` forks a JVM with the project's classes
    # on the classpath; `installDist` writes a start script that execs one with
    # the jars. The two share nothing on the command line except the main
    # class, which is exactly why the main class is what is matched.
    case "$_cmd" in
        *de.fiereu.openmmo.server.login.MainKt*) echo 'login server'; return 0 ;;
        *de.fiereu.openmmo.server.game.MainKt*)  echo 'game server';  return 0 ;;
    esac

    # And the wrapper sitting in front of one, which has to go too or the next
    # `./gradlew` waits on a task whose JVM is already gone.
    case "$_cmd" in
        *gradle-wrapper.jar*:server.login:run*) echo 'gradlew :server.login:run'; return 0 ;;
        *gradle-wrapper.jar*:server.game:run*)  echo 'gradlew :server.game:run';  return 0 ;;
    esac
    return 1
}

# Every TCP port a pid is listening on, as ss reports it. Own processes only,
# which these are; the trailing comma keeps pid=123 from matching pid=1234.
ports_of() {
    ss -ltnp 2>/dev/null | awk -v want="pid=$1," '
        index($0, want) { n = split($4, a, ":"); print a[n] }' | sort -un | tr '\n' ' '
}

alive() { kill -0 "$1" 2>/dev/null; }

# ------------------------------------------------------------------ find
FOUND=''
for d in /proc/[0-9]*; do
    pid=${d#/proc/}
    label=$(classify "$pid") || continue
    printf '  pid %-8s %-26s %s\n' "$pid" "$label" "$(ports_of "$pid")"
    FOUND="$FOUND $pid"
done

if [ -z "$FOUND" ]; then
    say 'no OpenMMO server is running'
    exit 0
fi

if [ "$DRY" -eq 1 ]; then
    say 'dry run, nothing was signalled'
    exit 0
fi

# ------------------------------------------------------------------ stop
say 'sending TERM'
for pid in $FOUND; do kill -TERM "$pid" 2>/dev/null || true; done

# A server closes its listeners and flushes on TERM, so it is worth waiting
# for. Fifteen seconds is longer than any clean shutdown here has taken.
n=0
while [ "$n" -lt 15 ]; do
    left=''
    for pid in $FOUND; do alive "$pid" && left="$left $pid"; done
    [ -n "$left" ] || break
    sleep 1
    n=$((n + 1))
done

if [ -n "${left:-}" ]; then
    warn "still up after ${n}s, sending KILL:$left"
    for pid in $left; do kill -KILL "$pid" 2>/dev/null || true; done
    sleep 2
fi

# ----------------------------------------------------------------- verify
# The proof is the port, not the exit of the kill. A JVM that ignored TERM and
# survived KILL is a kernel-level stuck process and has to be said out loud
# rather than reported as success.
rc=0
for pid in $FOUND; do
    if alive "$pid"; then warn "pid $pid is still alive"; rc=1; fi
done

LAUNCHER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/openmmo/launcher.cfg"
cfg_port=$(awk '$1 == "gameport" { print $2 }' "$LAUNCHER_CFG" 2>/dev/null | tail -1)
for port in 2106 7777 7778 ${OPENMMO_LOGIN_PORT:-} ${OPENMMO_GAMEPORT:-} ${cfg_port:-}; do
    if ss -ltn 2>/dev/null | grep -q ":$port "; then
        warn "port $port is still held:"
        ss -ltnp 2>/dev/null | grep ":$port " || true
        rc=1
    fi
done

[ "$rc" -eq 0 ] && say 'every OpenMMO server is down'
exit "$rc"
