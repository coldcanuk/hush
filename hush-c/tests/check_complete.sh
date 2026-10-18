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
base=$(mktemp -d)
home="$base/user"
bin_dir="$base/bin"
pid=""

cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "${base:-$home}"
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
export HUSH_HOME="$base/home"
export HUSH_CONFIG_DIR="$base/config"
unset XDG_CONFIG_HOME
mkdir -p "$bin_dir" "$home/.grok" "$base/home" "$base/config" "${HUSH_CONFIG_DIR:-$base/config}"
printf '%s\n' '#!/bin/sh' \
    'printf "%s\n" "int x;"' \
    > "$bin_dir/grok"
chmod 0755 "$bin_dir/grok"
PATH="$bin_dir:$PATH"
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
token=$(printf '%s' "$got" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
test -n "$token" || fail "token parse failed: $got"
# #361: the job token is a 128-bit CSPRNG secret, not a c<seq> counter.
printf '%s' "$token" | grep -Eq '^[0-9a-f]{32}$' || fail "token is not 32 lowercase hex: $token"

# #361 exploit regression. No session: 401 before any token compare.
for t in "$token" c1 c2; do
    code=$(command curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${port}/api/complete?t=${t}")
    [ "$code" = 401 ] || fail "no-session GET ?t=${t} must be 401 (got $code)"
done
# With the session, a guessed, longer, shorter or one-digit-off token
# names no job: never pending, never text, and it does not use up the job.
# (An over-long token is a 400 "bad request": never cut down to a match.)
refused() {
    r=$(curl -s "http://127.0.0.1:${port}/api/complete?t=$1")
    test -n "$r" || fail "$2 (no reply)"
    if printf '%s' "$r" | grep -q '"ok":true'; then fail "$2 ($r)"; fi
}
last=$(printf '%s' "$token" | cut -c32)
if [ "$last" = 0 ]; then flip=1; else flip=0; fi
refused c1 "a sequential c1 token must be refused"
refused c2 "a sequential c2 token must be refused"
refused "${token}0" "the token plus one character must be refused"
refused "${token}${token}" "the token doubled must be refused"
# #364 P2-2: an over-long ?t= is refused by the parser with a 400, never
# truncated into a lookup.
for t in "${token}0" "${token}${token}"; do
    code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${port}/api/complete?t=${t}")
    [ "$code" = 400 ] || fail "an over-long ?t= must be 400 (got $code)"
done
refused "${token%?}" "the token minus one character must be refused"
refused "${token%?}${flip}" "a one-digit-off token must be refused"

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
# #361: single use. A taken token reads nothing the second time.
refused "$token" "a taken token must not read the result again"

# #361: the next job gets an unrelated token; the old one stays dead.
got2=$(curl -sf -X POST "http://127.0.0.1:${port}/api/complete" \
    -H 'Content-Type: application/json' \
    -d '{"prefix":"int ","suffix":" = 2;"}')
token2=$(printf '%s' "$got2" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
printf '%s' "$token2" | grep -Eq '^[0-9a-f]{32}$' || fail "second token is not 32 hex: $got2"
[ "$token2" != "$token" ] || fail "two jobs must not share a token"
refused "$token" "the first job's token must not name the second job"

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
