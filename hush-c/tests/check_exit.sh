#!/bin/sh
# Close stays up; Exit / --quit stop the process with code 0.
set -eu
cd "$(dirname "$0")/.."
bin=./hush-relay
port=18768
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
pidfile=""
pid=""

pidfile_path() {
    if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
        printf '%s/hush/relay-%s.pid' "$XDG_RUNTIME_DIR" "$port"
    elif [ -n "${HOME:-}" ]; then
        printf '%s/.local/state/hush/relay-%s.pid' "$HOME" "$port"
    else
        printf '/tmp/hush/relay-%s.pid' "$port"
    fi
}

cleanup() {
    if [ -n "${fake_pid:-}" ]; then
        kill "$fake_pid" 2>/dev/null || true
        wait "$fake_pid" 2>/dev/null || true
    fi
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log" "${fake:-}"
    rm -rf "$cfg"
}
trap cleanup EXIT

fail() { echo "exit check failed: $1" >&2; exit 1; }

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

wait_down() {
    i=0
    while [ "$i" -lt 50 ]; do
        if ! kill -0 "$1" 2>/dev/null; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    return 1
}

"$bin" --help | grep -q -- '--quit' || fail "help missing --quit"
"$bin" --help | grep -q -- '--close' || fail "help missing --close"
"$bin" --close "$port" >/dev/null
test "$?" -eq 0 || fail "--close must exit 0"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"
pidfile=$(pidfile_path)
test -f "$pidfile" || fail "pidfile missing ($pidfile)"
grep -q "$(printf '%s' "$pid")" "$pidfile" || fail "pidfile pid mismatch"

# Fake a leftover --app window so Exit must reap it (the real browser
# is not spawned under --no-open). cmdline must contain both needles.
fake=$(mktemp)
printf '#!/bin/sh\nsleep 30\n' >"$fake"
chmod +x "$fake"
"$fake" --class=hush-relay --app="http://127.0.0.1:${port}/" >/dev/null 2>&1 &
fake_pid=$!
sleep 0.05
kill -0 "$fake_pid" 2>/dev/null || fail "fake app window did not start"
tr '\0' ' ' <"/proc/${fake_pid}/cmdline" | grep -q -- '--class=hush-relay' \
    || fail "fake cmdline missing class"

close=$(curl -sf -X POST "http://127.0.0.1:${port}/api/close" \
    -H 'Content-Type: application/json' -d '{}')
echo "$close" | grep -q '"action":"close"' || fail "close json"
curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null \
    || fail "close must leave the relay up"

exit_body=$(curl -sf -X POST "http://127.0.0.1:${port}/api/exit" \
    -H 'Content-Type: application/json' -d '{}')
echo "$exit_body" | grep -q '"action":"exit"' || fail "exit json"
wait_down "$pid" || fail "exit did not stop the process"
wait "$pid"
test "$?" -eq 0 || fail "exit must be code 0"
test ! -f "$pidfile" || fail "pidfile left after exit"
if kill -0 "$fake_pid" 2>/dev/null; then
    fail "exit left a --app child running"
fi
fake_pid=""
pid=""

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "second start failed"
"$bin" --quit "$port"
wait_down "$pid" || fail "--quit did not stop the process"
wait "$pid"
test "$?" -eq 0 || fail "--quit child must be code 0"
test ! -f "$(pidfile_path)" || fail "pidfile left after --quit"
pid=""

echo "exit routes ok"
