#!/bin/sh
# Check the CI gate offline, the one CI failure worth catching
# without a runner: a stage learns to run a program and nobody adds its
# package, or the workflow names a stage the script does not know.
set -eu

CI=${1:?usage: ci_test.sh <ci.sh> <workflow.yml>}
WF=${2:?usage: ci_test.sh <ci.sh> <workflow.yml>}

fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

run_exit() { set +e; "$@" >/dev/null 2>&1; echo $?; set -e; }

echo "the client CI gate is internally consistent:"

# 0. the script exists and runs
[ -x "$CI" ] && ok "ci.sh is present and executable" \
             || { bad "ci.sh $CI is missing or not executable"; echo "ci: FAILED"; exit 1; }

# The stages the dispatcher accepts, read from the script itself so this test
# tracks it rather than a hand-kept copy.
STAGES=$(grep -oE '^ *packages\|[a-z|]+\)' "$CI" | head -1 | tr -d ' )' | tr '|' ' ')
[ -n "$STAGES" ] && ok "the stage list parses ($STAGES)" \
                 || { bad "could not read the stage list from $CI"; echo "ci: FAILED"; exit 1; }

# 1. packages and tools print non-empty lists
pkgs=$("$CI" packages); tools=$("$CI" tools)
[ -n "$pkgs" ]  && ok "packages prints a non-empty list"  || bad "packages printed nothing"
[ -n "$tools" ] && ok "tools prints a non-empty list"     || bad "tools printed nothing"

# 2. every program's package (column 2 of tools) is one the packages stage lists
missing=""
echo "$tools" | while read -r prog pkg; do
    [ -n "$pkg" ] || continue
    printf '%s\n' "$pkgs" | grep -qx "$pkg" || echo "$prog->$pkg"
done > /tmp/ci_test_missing.$$
missing=$(cat /tmp/ci_test_missing.$$); rm -f /tmp/ci_test_missing.$$
[ -z "$missing" ] && ok "every tool's package is in the packages list" \
                  || bad "tools name packages not in the packages list: $missing"

# 3. every dry-runnable stage exits 0 and prints only planned (+ ) or ci: lines
for s in build test abicheck windows server all; do
    out=$("$CI" --dry-run "$s" 2>&1); rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "dry-run '$s' exited $rc"
    elif printf '%s\n' "$out" | grep -qvE '^(\+ |ci: )'; then
        bad "dry-run '$s' printed a line that is neither planned nor a note"
    else
        ok "dry-run '$s' plans cleanly (exit 0)"
    fi
done

# 4. an unknown stage is refused, not silently accepted
rc=$(run_exit "$CI" nosuchstage)
[ "$rc" -ne 0 ] && ok "an unknown stage is refused (exit $rc)" \
                || bad "an unknown stage should be refused, got exit 0"

# 5. every stage the workflow invokes is one ci.sh knows
wf_stages=$(grep -oE 'ci\.sh +[a-z]+' "$WF" | awk '{print $2}' | sort -u)
[ -n "$wf_stages" ] && ok "the workflow invokes ci.sh ($(echo $wf_stages | tr '\n' ' '))" \
                    || bad "the workflow $WF invokes no ci.sh stage"
for s in $wf_stages; do
    case " $STAGES " in
        *" $s "*) : ;;
        *) bad "the workflow names stage '$s', which ci.sh does not define" ;;
    esac
done

if [ "$fail" -eq 0 ]; then
    echo "ci: all checks passed"
else
    echo "ci: FAILED"
fi
exit $fail
