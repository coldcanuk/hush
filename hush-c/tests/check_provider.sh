#!/bin/sh
# Provider configure: GET/POST /api/provider, scan, no key echo.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18769
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

fail() { echo "provider check failed: $1" >&2; exit 1; }

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
unset XDG_CONFIG_HOME
mkdir -p "$home/.config/goose"
printf '%s\n' 'active_provider: xai_oauth' > "$home/.config/goose/config.yaml"
printf '%s\n' '  xai_oauth:' >> "$home/.config/goose/config.yaml"
printf '%s\n' '    model: grok-4.6' >> "$home/.config/goose/config.yaml"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"

got=$(curl -sf "http://127.0.0.1:${port}/api/provider")
echo "$got" | grep -q '"goose"' || fail "GET missing goose"
echo "$got" | grep -q '"openai-api"' || fail "GET missing openai"
echo "$got" | grep -q '"family":"home"' || fail "GET missing home family"
echo "$got" | grep -q '"family":"api"' || fail "GET missing api family"
echo "$got" | grep -q 'api_key' && fail "GET leaked api_key"
echo "$got" | grep -q 'sk-' && fail "GET leaked key material"

saved=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"openai-api","host":"https://api.openai.com","model":"gpt-4o","api_key":"sk-secret-test"}')
echo "$saved" | grep -q '"model":"gpt-4o"' || fail "POST did not save model"
echo "$saved" | grep -q 'sk-secret-test' && fail "POST echoed api_key"

again=$(curl -sf "http://127.0.0.1:${port}/api/provider")
echo "$again" | grep -q '"model":"gpt-4o"' || fail "GET lost model"
echo "$again" | grep -q 'sk-secret' && fail "later GET leaked key"

home_save=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"goose","use_home":"true"}')
echo "$home_save" | grep -q '"use_home":true' || fail "goose use_home"

scan=$(curl -s -X POST "http://127.0.0.1:${port}/api/provider/scan" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"openai-api","host":"http://127.0.0.1:1","api_key":"x"}')
echo "$scan" | grep -q '"ok":false' || fail "scan should fail closed host"
echo "$scan" | grep -q 'sk-' && fail "scan leaked key"

bad=$(curl -s -o /tmp/hush-bad-provider -w '%{http_code}' \
    -X POST "http://127.0.0.1:${port}/api/provider" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"nope"}')
test "$bad" != "200" || fail "unknown provider must fail"

echo "provider routes ok"
