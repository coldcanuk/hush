#!/bin/sh
# Canvas FIM must return a token immediately and never insert a hive note.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="$test_home/hush"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }

bin=./hush-relay
port=$(hush_free_port) || exit 1
log=$(mktemp)
home=$(mktemp -d)
pid=""

cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "$home"
}
trap cleanup EXIT

fail() { echo "complete check failed: $1" >&2; exit 1; }

wait_up() {
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    return 1
}

export HOME="$home"
export HUSH_HOME="$home/.hush"
export HUSH_CONFIG_DIR="$home/.config/hush"
unset XDG_CONFIG_HOME
mkdir -p "$home/bin" "$home/.grok" "$home/.config/hush"
printf '%s\n' '#!/bin/sh' \
    'printf "%s\n" "int x;"' \
    > "$home/bin/grok"
chmod 0755 "$home/bin/grok"
PATH="$home/bin:$PATH"
export PATH
printf '%s\n' '{"ok":true}' > "$home/.grok/auth.json"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"

curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"create"}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"ack_backup","save_pass":false}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' \
    -d '{"name":"HQ","about":"primary endpoint"}' >/dev/null

before=$(curl -sf "http://127.0.0.1:${port}/api/status")
before_n=$(printf '%s' "$before" | sed -n 's/.*"events":\([0-9]*\).*/\1/p')
test -n "$before_n" || fail "status events missing"

got=$(curl -sf -X POST "http://127.0.0.1:${port}/api/complete" \
    -H 'Content-Type: application/json' \
    -d '{"prefix":"int ","suffix":" = 1;"}')
printf '%s' "$got" | grep -q '"ok":true' || fail "complete start not ok: $got"
printf '%s' "$got" | grep -q '"token":"c' || fail "complete token missing: $got"
token=$(printf '%s' "$got" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
test -n "$token" || fail "token parse failed: $got"

text=""
i=0
while [ "$i" -lt 40 ]; do
    poll=$(curl -sf "http://127.0.0.1:${port}/api/complete?t=${token}")
    if printf '%s' "$poll" | grep -q '"text":'; then
        text=$poll
        break
    fi
    printf '%s' "$poll" | grep -q '"pending":true' || fail "unexpected poll: $poll"
    i=$((i + 1))
    sleep 0.05
done
test -n "$text" || fail "complete never returned text"
printf '%s' "$text" | grep -q 'int x;' || fail "complete text missing: $text"

after=$(curl -sf "http://127.0.0.1:${port}/api/status")
after_n=$(printf '%s' "$after" | sed -n 's/.*"events":\([0-9]*\).*/\1/p')
test "$before_n" = "$after_n" || fail "complete must not insert a hive note"

# Canvas FIM stays free of libcurl. The vault client is the one source
# allowed to include the real header and link it through pkg-config.
curl_hits=$(grep -R --include='*.c' --include='*.h' -l 'curl/curl.h' src include || true)
for hit in $curl_hits; do
    case "$hit" in
        src/hush_vault.c) ;;
        *) fail "sources include curl/curl.h" ;;
    esac
done
if grep -n -- '-lcurl' Makefile | grep -v 'pkg-config --libs libcurl' >/dev/null; then
    fail "Makefile links -lcurl"
fi
! grep -R --include='*.c' --include='*.h' -n 'pthread' src include >/dev/null \
    || fail "sources use pthread"

echo "complete FIM ok"
