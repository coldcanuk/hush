#!/bin/sh
# check_no_nsec_leak_selftest.sh — killing test for check_no_nsec_leak.sh.
# Plants an empty leak_nsec file in a fresh dir under /tmp, outside the
# run TMPDIR, and requires the guard to FAIL and name it; removes the plant
# and requires the guard to pass. Then does the same with a plant left
# inside the run TMPDIR. The planted files are empty: no key is written.
# Removes only the two dirs it made itself, on every exit path.
set -eu
cd "$(dirname "$0")/.."

guard=tests/check_no_nsec_leak.sh
PLANT_NAME=leak_nsec

fail() { echo "nsec leak self-test failed: $1" >&2; exit 1; }

run_dir=""
plant_dir=""
cleanup() {
    for made in "$plant_dir" "$run_dir"; do
        if [ -n "$made" ]; then
            rm -rf "$made"
        fi
    done
}
trap cleanup EXIT

# A stand-in run TMPDIR under the caller's TMPDIR, and a plant dir that is
# outside it by construction (directly under /tmp).
run_dir=$(mktemp -d)
plant_dir=$(mktemp -d /tmp/hush-leak-plant.XXXXXX)
case "$plant_dir/" in
    "$run_dir"/*) fail "plant dir $plant_dir is inside the run dir" ;;
esac
marker="$run_dir/.hush-leak-marker"
out="$run_dir/guard.out"
sh "$guard" mark "$marker"

sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard fails with nothing planted"; }

: > "$plant_dir/$PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $plant_dir/$PLANT_NAME planted outside the run dir"
fi
grep -q "$plant_dir/$PLANT_NAME" "$out" || fail "guard did not name the outside plant"
rm -f "$plant_dir/$PLANT_NAME"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the outside plant is gone"; }

: > "$run_dir/$PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $PLANT_NAME left inside the run dir"
fi
grep -q "/$PLANT_NAME" "$out" || fail "guard did not name the inside plant"
rm -f "$run_dir/$PLANT_NAME"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the inside plant is gone"; }

echo "nsec leak self-test ok: guard fails on both plants and passes without them"
