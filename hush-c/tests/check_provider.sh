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
export HUSH_PROVIDER_TERM=/bin/true
mkdir -p "$home/.config/goose" "$home/bin"
printf '%s\n' '#!/bin/sh' 'exit 0' > "$home/bin/grok"
chmod 0755 "$home/bin/grok"
PATH="$home/bin:$PATH"
export PATH
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
echo "$got" | grep -q '"api_key"' && fail "GET leaked api_key field"
echo "$got" | grep -q '"username"' && fail "GET leaked username field"
echo "$got" | grep -q '"password"' && fail "GET leaked password field"
echo "$got" | grep -q '"token"' && fail "GET leaked token field"
echo "$got" | grep -q '"passkey"' && fail "GET leaked passkey field"
echo "$got" | grep -q 'sk-' && fail "GET leaked key material"

saved=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"openai-api","host":"https://api.openai.com","model":"gpt-4o","api_key":"sk-secret-test","username":"user-alice","password":"pw-secret","token":"tok-secret","passkey":"pk-secret"}')
echo "$saved" | grep -q '"model":"gpt-4o"' || fail "POST did not save model"
echo "$saved" | grep -q 'sk-secret-test' && fail "POST echoed api_key"
echo "$saved" | grep -q 'user-alice' && fail "POST echoed username"
echo "$saved" | grep -q 'pw-secret' && fail "POST echoed password"
echo "$saved" | grep -q 'tok-secret' && fail "POST echoed token"
echo "$saved" | grep -q 'pk-secret' && fail "POST echoed passkey"
echo "$saved" | grep -q '"api_key"' && fail "POST leaked api_key field"

again=$(curl -sf "http://127.0.0.1:${port}/api/provider")
echo "$again" | grep -q '"model":"gpt-4o"' || fail "GET lost model"
echo "$again" | grep -q 'sk-secret' && fail "later GET leaked key"
echo "$again" | grep -q 'user-alice' && fail "later GET leaked username"
echo "$again" | grep -q 'pw-secret' && fail "later GET leaked password"
echo "$again" | grep -q 'tok-secret' && fail "later GET leaked token"
echo "$again" | grep -q 'pk-secret' && fail "later GET leaked passkey"
echo "$again" | grep -q '"api_key"' && fail "later GET leaked api_key field"
echo "$again" | grep -q '"username"' && fail "later GET leaked username field"
echo "$again" | grep -q '"password"' && fail "later GET leaked password field"
echo "$again" | grep -q '"token"' && fail "later GET leaked token field"
echo "$again" | grep -q '"passkey"' && fail "later GET leaked passkey field"

overlay="$home/.config/hush/providers.json"
test -f "$overlay" || fail "overlay missing"
grep -q 'sk-secret-test' "$overlay" && fail "overlay stored api_key"
grep -q 'user-alice' "$overlay" && fail "overlay stored username"
grep -q 'pw-secret' "$overlay" && fail "overlay stored password"
grep -q 'tok-secret' "$overlay" && fail "overlay stored token"
grep -q 'pk-secret' "$overlay" && fail "overlay stored passkey"

sess=$(curl -sf "http://127.0.0.1:${port}/api/session")
echo "$sess" | grep -q 'sk-secret-test' && fail "session leaked api_key"
echo "$sess" | grep -q 'pw-secret' && fail "session leaked password"

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

login=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider/login" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"grok-build"}')
echo "$login" | grep -q '"ok":true' || fail "grok login should start"
echo "$login" | grep -q '"ok":false' && fail "grok login reported false"

goose_login=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider/login" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"goose"}')
echo "$goose_login" | grep -q '"ok":false' || fail "goose login should refuse"
echo "$goose_login" | grep -q 'login not offered' || fail "goose login message"

unknown_login=$(curl -sf -X POST "http://127.0.0.1:${port}/api/provider/login" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"nope"}')
echo "$unknown_login" | grep -q '"ok":false' || fail "unknown login should refuse"
echo "$unknown_login" | grep -q 'unknown provider' || fail "unknown login message"

echo "provider routes ok"
