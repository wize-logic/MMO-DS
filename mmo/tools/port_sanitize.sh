#!/bin/sh
# Drive an already-built sanitizer tree of the fused build, and say what the
# sanitizer found.
#
#   port_sanitize.sh asan|ubsan <mmo-root> <sanitizer-build> <engine-dir> [rom]
set -eu

usage() {
    echo "usage: port_sanitize.sh asan|ubsan <mmo-root> <build> <engine> [rom]" >&2
    exit 2
}

[ $# -ge 4 ] || usage
mode=$1
root=$2
sanbuild=$3
engine=$4
rom=${5:-$engine/build/rom/pokeplatinum.us.nds}

case $mode in
asan | ubsan) ;;
*) usage ;;
esac

bin=$sanbuild/fused/pokeplatinum
if [ ! -x "$bin" ]; then
    echo "$mode: SKIP (no sanitizer build at $bin)"
    exit 0
fi
if [ ! -f "$rom" ]; then
    echo "$mode: SKIP (no ROM at $rom)"
    exit 0
fi

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

PC_ROM=$rom
export PC_ROM

# Which of the engine's suite runs under the sanitizer. The engine's own asan
# and ubsan targets pick determinism + replay + corpus; `replay` is dropped here
# for the reason the design notes gives, a build that changed the heap layout on
# purpose cannot match a pinned memory image, and it would fail every run
# without saying anything a sanitizer knows.
tests=${PORT_SAN_TESTS:-determinism corpus}

if [ "$mode" = asan ]; then
    # Not a second copy of the engine's ASan options: read out of its Makefile,
    # because two of the three are load-bearing (detect_odr_violation=0 in
    # particular, the ARM7 objcopy rename makes ASan see a false ODR pair) and
    # a copy here is how one of them goes stale. log_path sends every report to
    # a file we can read afterwards; without it they go to each boot's own log
    # inside a test's temporary directory and vanish with it.
    base=$(sed -n 's/^ASAN_OPTIONS_BASE := //p' "$engine/pc/Makefile")
    if [ -z "$base" ]; then
        echo "port_sanitize: ASAN_OPTIONS_BASE is no longer in $engine/pc/Makefile;" >&2
        echo "  find what replaced it rather than guessing the options here." >&2
        exit 1
    fi
    ASAN_OPTIONS="$base:halt_on_error=0:log_path=$out/asan"
    export ASAN_OPTIONS
else
    # print_stacktrace is what puts a function name on a report; without it a
    # UBSan line is a file and a column in a generated .stripped.c and says
    # nothing about which code did it.
    UBSAN_OPTIONS="print_stacktrace=1:log_path=$out/ubsan"
    export UBSAN_OPTIONS
fi

fail=0

echo "$mode: the engine's suite over the fused build ($tests)"
# Through a view of the engine whose build/ is this sanitizer tree, for the two
# reasons tools/port_root.sh gives: the suite mints saves into the root it
# thinks it is in, and a station finds the ROM only from the working directory.
view=$sanbuild/portroot
"$root/tools/port_root.sh" "$engine" "$view" "$bin" > /dev/null
# shellcheck disable=SC2086
(cd "$view" && python3 pc/tests/run_tests.py $tests) || fail=1

echo "$mode: this client's own fused checks"
for t in crowd heap name label; do
    "$root/tests/${t}_test.sh" "$root" "$sanbuild" "$engine" || fail=1
done

# The two reports that are the engine's and not this client's.
#
#   BerryPatches_Init reads two bytes past sBerryInitTable on every boot that
#   mints a save: it loops to MAX_BERRY_PATCHES (128) while the table it is
#   given (include/data/berry_init.h) holds 118 entries and its caller passes
#   118 as the size, the condition is `i < MAX_BERRY_PATCHES || i < initSize`,
#   an or, so the last ten patches on a new save come out of whatever the
#   linker put after the table.
#   Task_ThrowTrainerBall indexes a s16[6][2] at 6 on every ball thrown
#   (src/battle/battle_display.c). Every station with a battle in it crosses
#   this, so a halting UBSan cannot see past the first fight.
known_report() {
    case $1 in
    "global-buffer-overflow in BerryPatches_Init") return 0 ;;
    "Task_ThrowTrainerBall: index "*" out of bounds for type 's16 [6][2]'") return 0 ;;
    esac
    return 1
}

# One signature per report: what the sanitizer called it and which function it
# was in. ASan writes one report per file and names the kind on its ERROR line;
# UBSan writes as many as the run hit, each a `runtime error:` line whose
# function is on the `#0` frame under it.
sigs=$out/signatures
: > "$sigs"
for f in "$out"/"$mode".*; do
    [ -f "$f" ] || continue
    awk -v mode="$mode" '
        mode == "asan" && /ERROR: AddressSanitizer: / {
            sub(/^.*ERROR: AddressSanitizer: /, ""); sub(/ .*$/, "");
            kind = $0; want = 1; next
        }
        mode == "ubsan" && / runtime error: / {
            if (want) print kind ": an unnamed frame";
            sub(/^.* runtime error: /, ""); kind = $0; want = 1; next
        }
        want && /^ *#0 0x[0-9a-f]+ in / {
            print (mode == "asan" ? kind " in " $4 : $4 ": " kind);
            want = 0
        }
        END { if (want) print kind (mode == "asan" ? " in " : ": ") "an unnamed frame" }
    ' "$f" >> "$sigs"
done

if [ ! -s "$sigs" ]; then
    echo "$mode: not one report, over every boot the runs above took"
else
    # Fed from a file rather than a pipe on purpose: a `while read` behind a
    # pipe is a subshell, and the verdict it reached would not come back out.
    sort "$sigs" | uniq -c > "$out/tally"
    while read -r n sig; do
        if known_report "$sig"; then
            echo " known $sig (x$n), the engine's, see port_sanitize.sh"
        else
            echo "  NEW    $sig (x$n)"
            echo "$mode: a report that is not one of the known two. Read it"
            echo " before adding it to known_report(), every entry on that"
            echo "  list has a measured reason beside it, and this one has none."
            fail=1
        fi
    done < "$out/tally"
fi

[ "$fail" -eq 0 ] || { echo "$mode: FAILED"; exit 1; }
echo "$mode: all checks passed"
