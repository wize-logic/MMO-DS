#!/bin/sh
# Drive the actual shipped client binary, not a test that
# links its own copy of the netcode.
set -eu

BIN=${1:?usage: realbin_test.sh <client-binary> <trace-file>}
TRACE=${2:?usage: realbin_test.sh <client-binary> <trace-file>}

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ok DESC, a check that passed
# bad DESC, a check that failed (records failure, keeps going)
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# expect_exit WANT DESC, run "$@" from the 3rd arg on; assert its exit code.
# (Runs the remaining args as the command.)
run_exit() { set +e; "$@" >/dev/null 2>&1; echo $?; set -e; }

echo "the shipped client binary replays and vets a recorded server trace:"

# 0. the artifact exists and is the real product binary
[ -x "$BIN" ] && ok "client binary is present and executable" \
              || { bad "client binary $BIN is missing"; echo "realbin: FAILED"; exit 1; }

# 1. happy path: the committed trace replays clean, exit 0
out=$("$BIN" replay --trace "$TRACE" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q "replays clean"; then
    ok "committed login trace replays clean (exit 0)"
else
    bad "committed login trace should replay clean (rc=$rc: $out)"
fi

# 2. a missing trace is a loud non-zero, not a silent pass
rc=$(run_exit "$BIN" replay --trace "$tmp/nope.trace")
[ "$rc" -ne 0 ] && ok "a missing trace file fails (exit $rc)" \
                || bad "a missing trace file should fail, got exit 0"

# 3. render-pin flip: bytes intact, the pinned rendered state wrong.
#    A decode regression that keeps the wire bytes is what this catches.
grep -q "render login-state=0" "$TRACE" \
    || { bad "trace has no 'render login-state=0' pin to corrupt"; }
sed 's/render login-state=0/render login-state=2/' "$TRACE" > "$tmp/render.trace"
cmp -s "$TRACE" "$tmp/render.trace" \
    && bad "render-pin corruption changed nothing" \
    || {
        rc=$(run_exit "$BIN" replay --trace "$tmp/render.trace")
        [ "$rc" -ne 0 ] && ok "a wrong render pin is rejected with bytes intact (exit $rc)" \
                        || bad "a wrong render pin should be rejected, got exit 0"
    }

# 4. wire-byte flip: flip one ciphertext nibble in the server's app frame.
#    Targets the s2c app line structurally (a fixed column well inside the
#    ciphertext), so it survives the trace being re-recorded.
awk 'BEGIN{done=0}
     /^s2c app / && !done {
         i=20; c=substr($0,i,1); n=(c=="a"?"b":"a");
         $0=substr($0,1,i-1) n substr($0,i+1); done=1
     }
     {print}' "$TRACE" > "$tmp/wire.trace"
cmp -s "$TRACE" "$tmp/wire.trace" \
    && bad "wire-byte corruption changed nothing" \
    || {
        rc=$(run_exit "$BIN" replay --trace "$tmp/wire.trace")
        [ "$rc" -ne 0 ] && ok "a flipped ciphertext byte is rejected (exit $rc)" \
                        || bad "a flipped ciphertext byte should be rejected, got exit 0"
    }

# 5. the binary's own primitive self-test still passes end to end
rc=$(run_exit "$BIN" selftest)
[ "$rc" -eq 0 ] && ok "the binary's built-in selftest passes (exit 0)" \
                || bad "the binary's selftest should pass, got exit $rc"

if [ "$fail" -eq 0 ]; then
    echo "realbin: all checks passed"
else
    echo "realbin: FAILED"
fi
exit $fail
