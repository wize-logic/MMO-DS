#!/bin/sh
# Replay this repo's engine patches over the engine's own history and report
# what each one does at every revision: apply cleanly, apply at an offset,
# apply only with fuzz (context partly matched, the silent-wrong-place
# case), or fail outright.
#
#   patch_survival.sh [--engine DIR] [--limit N] [--head] [--gate]
#     --limit N   sample at most N of the revisions that touched each patched
#                 file (newest first; default 40)
#     --head      only the engine's current HEAD
#     --gate      --head, and exit non-zero unless every hunk landed on exact
#                 context.  This is the case worth failing on: the engine's own
#                 compile rule applies our diffs with `patch --forward` at the
#                 default fuzz, so a hunk whose context has rotted still lands,
#                 somewhere, and the build says nothing.  A hunk that stops
#                 applying altogether is already loud, the object is dropped
#                 and the link fails on its symbols.  This covers the other one.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/engine_pipeline.sh"
engine=${ENGINE:-$root/../engine/pokeplatinum}
patches=$root/mods/openmmo/patches
limit=40
head_only=0
gate=0

while [ $# -gt 0 ]; do
    case $1 in
    --engine) engine=$2; shift 2 ;;
    --limit) limit=$2; shift 2 ;;
    --head) head_only=1; shift ;;
    --gate) gate=1; head_only=1; shift ;;
    *) echo "patch_survival: unknown argument $1" >&2; exit 2 ;;
    esac
done

if ! git -C "$engine" rev-parse HEAD >/dev/null 2>&1; then
    echo "patch_survival: SKIP (no engine git checkout at $engine)"
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# One patch file at one revision.  Prints the verdict word.
#
# The two steps the compile takes before ours, strip_asm.py and the engine's
# own pc/patches diff, are engine_pipeline_prepare; replaying without them
# reported 0 clean at the pinned commit, where the build demonstrably works.
verdict() {
    _patch=$1 _file=$2 _rev=$3
    rm -rf "$tmp/w"
    mkdir -p "$tmp/w/$(dirname "$_file")"
    if ! git -C "$engine" show "$_rev:$_file" >"$tmp/w/$_file" 2>/dev/null; then
        echo "absent"
        return
    fi
    if ! engine_pipeline_prepare "$engine" "$_file" "$tmp/w"; then
        echo "base-gone"
        return
    fi
    # -F0: context must match exactly.  Offsets are still allowed and are
    # reported by patch itself, so they are picked out of the log.
    if (cd "$tmp/w" && patch -p1 --dry-run -F0 --silent <"$_patch" >"$tmp/log" 2>&1); then
        if grep -q "with fuzz" "$tmp/log"; then
            echo "fuzz"
        elif grep -q "offset" "$tmp/log"; then
            echo "offset"
        else
            echo "clean"
        fi
        return
    fi
    # Exact context failed.  Does the engine's default fuzz let it land anyway?
    # That is the dangerous answer: it applies, somewhere, and says nothing.
    if (cd "$tmp/w" && patch -p1 --dry-run --silent <"$_patch" >/dev/null 2>&1); then
        echo "fuzz-only"
    else
        echo "fail"
    fi
}

echo "engine: $engine at $(git -C "$engine" rev-parse --short HEAD)"
overall_fuzz=0
overall_bad=0

for p in $(find "$patches" -name '*.patch' | sort); do
    rel=${p#"$patches"/}
    file=${rel%.patch}
    # A patch whose base is NOT IN THE ENGINE'S history at all. That is a
    # meson wrap under subprojects/, the engine's own Makefile says those
    # "are meson wraps that a re-download would reset", so they are never
    # committed and `git show <rev>:<file>` misses at every revision. Reporting
    # that as "absent" and failing the gate says the patch rotted, which is a
    # different and false thing: there is no history to rot against. The
    # compile still applies it, and a hunk that stops landing there still fails
    # loudly into the skip count. Counted separately and not gated.
    if [ -z "$(git -C "$engine" log --all --format=%H -n 1 -- "$file" 2>/dev/null)" ]; then
        if [ -f "$engine/$file" ]; then
            printf '%s: unversioned (a meson wrap), the compile applies it, this cannot check it\n' "$file"
        else
            printf '  %-40s %s\n' "$file" "NOT IN THE ENGINE TREE"
            overall_bad=$((overall_bad + 1))
        fi
        continue
    fi
    if [ "$head_only" = 1 ]; then
        revs=$(git -C "$engine" rev-parse HEAD)
    else
        revs=$(git -C "$engine" log --format=%H -n "$limit" -- "$file")
        revs="$(git -C "$engine" rev-parse HEAD)
$revs"
    fi
    n=0 clean=0 offset=0 fuzz=0 fail=0 absent=0 basegone=0
    for r in $(echo "$revs" | awk '!seen[$0]++'); do
        v=$(verdict "$p" "$file" "$r")
        n=$((n + 1))
        case $v in
        clean) clean=$((clean + 1)) ;;
        offset) offset=$((offset + 1)) ;;
        fuzz | fuzz-only) fuzz=$((fuzz + 1)); overall_fuzz=$((overall_fuzz + 1)) ;;
        fail) fail=$((fail + 1)) ;;
        absent) absent=$((absent + 1)) ;;
        base-gone) basegone=$((basegone + 1)) ;;
        esac
        if [ "$v" != clean ] && [ "$head_only" = 1 ]; then
            overall_bad=$((overall_bad + 1))
        fi
        if [ "$v" != clean ]; then
            printf '  %-40s %s  %s\n' "$file" "$(git -C "$engine" rev-parse --short "$r")" "$v"
        fi
    done
    printf '%s: %d revisions, %d clean, %d offset, %d fuzz, %d fail, %d absent, %d base-gone\n' \
        "$file" "$n" "$clean" "$offset" "$fuzz" "$fail" "$absent" "$basegone"
done

if [ "$overall_fuzz" -gt 0 ]; then
    echo "note: a hunk that lands only with fuzz has matched partial context, read it before trusting it"
fi

if [ "$gate" = 1 ]; then
    if [ "$overall_bad" -gt 0 ]; then
        echo "patchcheck: FAIL, $overall_bad patch(es) no longer land on exact context."
        echo "  Re-cut the diff against what the compile consumes (strip_asm.py, then"
        echo "  the engine's own pc/patches diff, then ours) before trusting the build."
        exit 1
    fi
    echo "patchcheck: ok (every hunk on exact context)"
fi
