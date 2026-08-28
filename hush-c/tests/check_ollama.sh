#!/bin/sh
# Ollama robot: must spawn `ollama run <model> <combined>`, not grok, and the
# configured model plus the combined prompt must reach the right arguments.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18781
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

fail() { echo "ollama check failed: $1" >&2; exit 1; }

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
mkdir -p "$home/bin" "$home/.grok" "$home/.config/hush" "$home/.hush"
printf '%s\n' '#!/bin/sh' \
    'log="${HUSH_CONFIG_DIR}/ollama-run.log"' \
    'for a in "$@"; do printf "%s\n" "$a" >> "$log"; done' \
    'printf "%s\n" "OLLAMA_REPLY_MARKER"' \
    > "$home/bin/ollama"
chmod 0755 "$home/bin/ollama"
# Seeded robots are grok-build; provide a stub so they stay quiet.
printf '%s\n' '#!/bin/sh' 'printf "%s\n" "grok stub"' > "$home/bin/grok"
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

# Ollama local inference needs a configured model name.
curl -sf -X POST "http://127.0.0.1:${port}/api/provider" \
    -H 'Content-Type: application/json' \
    -d '{"provider":"ollama","model":"llama3"}' >/dev/null

ag=$(curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
    -H 'Content-Type: application/json' \
    -d '{"name":"Local","system_prompt":"Answer briefly.","provider":"ollama","save_pass":false}')
echo "$ag" | grep -q '"slug":"local"' || fail "local not raised"
npub=$(printf '%s' "$ag" | sed -n 's/.*"slug":"local"[^}]*"npub":"\([^"]*\)".*/\1/p')
test -n "$npub" || fail "local npub missing"

curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
    -H 'Content-Type: application/json' \
    -d "{\"content\":\"nostr:${npub} Hello from a human\",\"kind\":1,\"channel\":\"general\",\"mention_0\":\"${npub}\"}" \
    >/dev/null

got=""
i=0
while [ "$i" -lt 40 ]; do
    got=$(curl -sf "http://127.0.0.1:${port}/api/events")
    printf '%s' "$got" | grep -q 'OLLAMA_REPLY_MARKER' && break
    i=$((i + 1))
    sleep 0.05
done
printf '%s' "$got" | grep -q 'OLLAMA_REPLY_MARKER' || fail "ollama reply missing"

# The ollama binary must have been invoked with `run <model> <combined>`,
# proving it ran ollama (not grok) and the model came from the overlay.
test -f "$home/.config/hush/ollama-run.log" || fail "ollama run log missing"
grep -q '^llama3$' "$home/.config/hush/ollama-run.log" \
    || fail "ollama missing configured model"
grep -q 'Answer briefly' "$home/.config/hush/ollama-run.log" \
    || fail "ollama prompt missing robot system prompt"
grep -q 'Hello from a human' "$home/.config/hush/ollama-run.log" \
    || fail "ollama prompt missing human note"

echo "ollama routes ok"
