#!/bin/sh
# check_fake_modes.sh — pins the key-store fakes' file modes. Under a loose
# umask (022), fake-pass.sh, fake-op.py and fake-secret-tool.py must store
# every secret as a 0600 file in 0700 dirs (fake-op's items/ and vault dirs
# included), both for a fresh store and when the file and dirs already
# exist with wider modes (0644 / 0755): a fake must never keep the old mode.
# Modes are read with stat. One more step pins fake-pass.sh's write order: a
# reader holding an fd to an old 0644 file must not see the new secret, so
# the secret goes to a new file, never into the old one before its chmod.
# That read sees only the fixed probe text written here, never a real key.
set -eu
cd "$(dirname "$0")/.."

SECRET_TEXT=fake-mode-probe
OP_VAULT=Hush
OP_TITLE=hush-agents_probe_nsec
ITEM=agents/probe/nsec
FLAT_ITEM=agents_probe_nsec

work=""
cleanup() {
    if [ -n "$work" ]; then
        rm -rf "$work"
    fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() { echo "fake modes check failed: $1" >&2; exit 1; }

# want <octal mode> <path>: fails unless path has exactly that mode.
want() {
    got=$(stat -c '%a' -- "$2" 2>/dev/null || echo missing)
    [ "$got" = "$1" ] || fail "$2 is $got, want $1"
}

# loosen <file> <dir>...: pre-creates the file at 0644 inside 0755 dirs.
loosen() {
    file=$1
    shift
    for d in "$@"; do
        mkdir -p "$d"
        chmod 755 "$d"
    done
    : > "$file"
    chmod 644 "$file"
}

store_pass() {
    printf '%s' "$SECRET_TEXT" | HUSH_FAKE_PASS_DIR="$1" sh tests/fake-pass.sh save "$ITEM"
}

store_op() {
    printf '{"fields":[{"id":"password","value":"%s"}]}' "$SECRET_TEXT" |
        HUSH_FAKE_OP_DIR="$1" python3 tests/fake-op.py item create \
            --vault "$OP_VAULT" --title "$OP_TITLE" -
}

store_secret_tool() {
    printf '%s' "$SECRET_TEXT" | HUSH_FAKE_SECRET_DIR="$1" python3 tests/fake-secret-tool.py \
        store --label probe service hush item "$ITEM"
}

work=$(mktemp -d)
umask 022

for state in fresh loose; do
    pass="$work/$state-pass"
    op="$work/$state-op"
    sec="$work/$state-sec"
    if [ "$state" = loose ]; then
        loosen "$pass/$FLAT_ITEM" "$pass"
        loosen "$op/items/$OP_VAULT/$OP_TITLE" "$op" "$op/items" "$op/items/$OP_VAULT"
        loosen "$sec/$FLAT_ITEM" "$sec"
    fi
    store_pass "$pass" || fail "$state: fake-pass save failed"
    want 700 "$pass"
    want 600 "$pass/$FLAT_ITEM"
    store_op "$op" || fail "$state: fake-op item create failed"
    want 700 "$op"
    want 700 "$op/items"
    want 700 "$op/items/$OP_VAULT"
    want 600 "$op/items/$OP_VAULT/$OP_TITLE"
    store_secret_tool "$sec" || fail "$state: fake-secret-tool store failed"
    want 700 "$sec"
    want 600 "$sec/$FLAT_ITEM"
done
# A reader that opened the old 0644 file before the save keeps that inode.
# fake-pass removes it and writes a new 0600 file, so the held fd reads
# nothing; writing first and chmod after would show the probe through it.
held="$work/held-pass"
loosen "$held/$FLAT_ITEM" "$held"
exec 3< "$held/$FLAT_ITEM"
store_pass "$held" || fail "held: fake-pass save failed"
seen=$(cat <&3)
exec 3<&-
[ -z "$seen" ] || fail "a reader holding the old 0644 file saw the saved secret"
want 600 "$held/$FLAT_ITEM"

echo "fake modes ok: pass, op and secret-tool store 0600 files in 0700 dirs, fresh and over 0644/0755; a held fd to the old 0644 pass file sees no secret"
