#!/bin/sh
# check_no_nsec_leak_selftest.sh — killing test for check_no_nsec_leak.sh.
# Plants an empty leak_nsec file in a fresh dir under /tmp, outside the
# run TMPDIR, and requires the guard to FAIL and name it; removes the plant
# and requires the guard to pass. Does the same with an empty session.token
# (the relay session token name, HUSH_AUTH_FILE) outside the run TMPDIR,
# with an empty keys.vault outside it, with a leak_nsec plant left inside
# the run TMPDIR, and with a session.token and a keys.vault each left in a
# C-test-style private root (hush-auth-plant, hush-vault-plant) inside it.
# Then, for each of the other secret names (a *vault_* entry, a *providers_*
# entry, ui.jar and ui.nonce): planted outside the run TMPDIR the guard must
# FAIL and name it; planted at the top of the run TMPDIR it must pass and
# list it as INFO. The planted files are empty: no key or token is written.
# Removes only the two dirs it made itself, on every exit path.
set -eu
cd "$(dirname "$0")/.."

guard=tests/check_no_nsec_leak.sh
PLANT_NAME=leak_nsec
TOKEN_PLANT_NAME=session.token
ROOT_PLANT_DIR=hush-auth-plant
VAULT_PLANT_NAME=keys.vault
VAULT_ROOT_PLANT_DIR=hush-vault-plant
# One name per remaining secret pattern: *vault_*, *providers_*, ui.jar, ui.nonce.
OTHER_PLANT_NAMES="plant_vault_opener plant_providers_key ui.jar ui.nonce"

fail() { echo "nsec leak self-test failed: $1" >&2; exit 1; }

# Outside the run dir the guard must fail and name the plant; at the top of
# the run dir it must pass and name it as INFO; with it gone it must pass.
check_other_plant() {
    name=$1
    : > "$plant_dir/$name"
    if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
        fail "guard passed with $plant_dir/$name planted outside the run dir"
    fi
    grep -q "$plant_dir/$name" "$out" || fail "guard did not name the outside $name plant"
    rm -f "$plant_dir/$name"
    : > "$run_dir/$name"
    sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
        || { cat "$out" >&2; fail "guard fails with $name at the top of the run dir"; }
    grep -q "$run_dir/$name" "$out" || fail "guard did not list the inside $name plant as INFO"
    rm -f "$run_dir/$name"
    sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
        || { cat "$out" >&2; fail "guard still fails after the $name plants are gone"; }
}

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
# dash runs no EXIT trap on a signal: exit nonzero, through cleanup, instead.
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

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

: > "$plant_dir/$TOKEN_PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $plant_dir/$TOKEN_PLANT_NAME planted outside the run dir"
fi
grep -q "$plant_dir/$TOKEN_PLANT_NAME" "$out" || fail "guard did not name the outside token plant"
rm -f "$plant_dir/$TOKEN_PLANT_NAME"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the outside token plant is gone"; }

: > "$plant_dir/$VAULT_PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $plant_dir/$VAULT_PLANT_NAME planted outside the run dir"
fi
grep -q "$plant_dir/$VAULT_PLANT_NAME" "$out" || fail "guard did not name the outside vault plant"
rm -f "$plant_dir/$VAULT_PLANT_NAME"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the outside vault plant is gone"; }

: > "$run_dir/$PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $PLANT_NAME left inside the run dir"
fi
grep -q "/$PLANT_NAME" "$out" || fail "guard did not name the inside plant"
rm -f "$run_dir/$PLANT_NAME"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the inside plant is gone"; }

mkdir "$run_dir/$ROOT_PLANT_DIR"
: > "$run_dir/$ROOT_PLANT_DIR/$TOKEN_PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $TOKEN_PLANT_NAME left in a private root inside the run dir"
fi
grep -q "/$ROOT_PLANT_DIR/$TOKEN_PLANT_NAME" "$out" || fail "guard did not name the private-root token plant"
rm -f "$run_dir/$ROOT_PLANT_DIR/$TOKEN_PLANT_NAME"
rmdir "$run_dir/$ROOT_PLANT_DIR"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the private-root token plant is gone"; }

mkdir "$run_dir/$VAULT_ROOT_PLANT_DIR"
: > "$run_dir/$VAULT_ROOT_PLANT_DIR/$VAULT_PLANT_NAME"
if sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1; then
    fail "guard passed with $VAULT_PLANT_NAME left in a private root inside the run dir"
fi
grep -q "/$VAULT_ROOT_PLANT_DIR/$VAULT_PLANT_NAME" "$out" || fail "guard did not name the private-root vault plant"
rm -f "$run_dir/$VAULT_ROOT_PLANT_DIR/$VAULT_PLANT_NAME"
rmdir "$run_dir/$VAULT_ROOT_PLANT_DIR"
sh "$guard" check "$marker" "$run_dir" >"$out" 2>&1 \
    || { cat "$out" >&2; fail "guard still fails after the private-root vault plant is gone"; }

for name in $OTHER_PLANT_NAMES; do
    check_other_plant "$name"
done

echo "nsec leak self-test ok: guard fails on all ten failing plants (key, token, vault, *vault_*, *providers_*, ui.jar and ui.nonce outside; key inside; token and vault in a private root), lists the four other names as INFO inside, and passes without them"
