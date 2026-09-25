#!/bin/sh
# check_make_guard.sh — the rebuild guard fails loud on a live relay and
# passes when the port is free; `make clean`'s pre-kill reaps CHILD-mode
# turnserver only and never touches the systemd daemon.
#
# Covers scripts/check-relay-port.sh (wired into top-level `make` / `make
# install` via the `guard` target) and scripts/kill-relay.sh (wired into
# top-level `make clean`). Packaging-only: no relay behavior is changed.
set -eu
cd "$(dirname "$0")/.."

root=$(cd .. && pwd)
guard="$root/scripts/check-relay-port.sh"
killsh="$root/scripts/kill-relay.sh"
topmk="$root/Makefile"

fail() { echo "make guard check failed: $1" >&2; exit 1; }

[ -x "$guard" ] || fail "guard missing or not executable ($guard)"
[ -x "$killsh" ] || [ -f "$killsh" ] || fail "kill script missing ($killsh)"
sh -n "$guard" || fail "guard has a syntax error"
sh -n "$killsh" || fail "kill script has a syntax error"

# --- static: make wires the guard into build + install, never clean ---
grep -q 'check-relay-port' "$topmk" || fail "top Makefile never calls the guard"
grep -q '^all: guard' "$topmk" || fail "top 'all' skips the guard"
grep -q '^install: guard' "$topmk" || fail "top 'install' skips the guard"
grep -q 'kill-relay' "$topmk" || fail "top Makefile lost the clean pre-kill"

# --- static: kill script reaps CHILD state only, never the daemon ---
# (Strip comment lines: the header names the daemon paths it avoids.)
grep -q 'turnserver.pid' "$killsh" || fail "kill script ignores CHILD turnserver"
code=$(grep -v '^[[:space:]]*#' "$killsh")
echo "$code" | grep -q 'systemctl' && fail "kill script must never call systemctl"
echo "$code" | grep -q 'hush-turn.service' && fail "kill script must never reference the daemon unit"
echo "$code" | grep -q '/run/hush-turn' && fail "kill script must never touch the daemon pidfile"

# --- hermetic harness (never the operator's hive or state) ---
test_home="$(mktemp -d)"
export HUSH_HOME="$test_home/hush"
export HUSH_CONFIG_DIR="$test_home/cfg"
export XDG_RUNTIME_DIR="$test_home/run"
export HUSH_STATE_DIR="$test_home/state"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$test_home/pass"
mkdir -p "$HUSH_CONFIG_DIR" "$XDG_RUNTIME_DIR" "$HUSH_STATE_DIR" "$HUSH_FAKE_PASS_DIR"

bin=./hush-relay
port=18783
log="$test_home/relay.log"
pid=""
child_pid=""
daemon_pid=""
n1_pid=""

cleanup() {
    if [ -n "$child_pid" ]; then
        kill -KILL "$child_pid" 2>/dev/null || true
    fi
    if [ -n "$daemon_pid" ]; then
        kill -KILL "$daemon_pid" 2>/dev/null || true
    fi
    if [ -n "${n1_pid:-}" ]; then
        kill "$n1_pid" 2>/dev/null || true
        wait "$n1_pid" 2>/dev/null || true
    fi
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$test_home"
}
trap cleanup EXIT

wait_up() {
    up_port="${1:-$port}"
    i=0
    while [ "$i" -lt 100 ]; do
        if curl -sf "http://127.0.0.1:${up_port}/api/status" >/dev/null 2>&1; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    return 1
}

wait_down() {
    i=0
    while [ "$i" -lt 100 ]; do
        if ! kill -0 "$1" 2>/dev/null; then
            return 0
        fi
        i=$((i + 1))
        sleep 0.05
    done
    return 1
}

# --- guard passes when the port is free ---
HUSH_PORT="$port" sh "$guard" || fail "guard tripped with nothing running"

# --- curl-less stub PATH (P1): sh builtins plus only what the guard needs ---
# PATH assignments apply before command lookup, so the stub must also
# provide `sh` itself. Inside the guard only shell builtins run (command,
# kill, read, printf, test) plus ps/awk/tr/cat. curl is deliberately
# absent so the probe path is unreachable.
stub_bin="$test_home/stub-bin"
mkdir -p "$stub_bin"
for tool in sh ps awk tr cat; do
    ln -s "$(command -v "$tool")" "$stub_bin/$tool"
done
no_curl_path="$stub_bin"

# --- P1a: no relay + no curl still passes (exit 0) ---
# Self-contained: skip when a hush-relay is already running anywhere, which
# would trip the guard (exit 2) for reasons unrelated to this case.
if ps -axo comm= 2>/dev/null | grep -qx 'hush-relay'; then
    echo "skip: no-relay guard test needs no hush-relay running"
else
PATH="$no_curl_path" HUSH_PORT="$port" sh "$guard" \
    || fail "guard failed with no relay and no curl"
fi

# --- guard trips with a live --no-open relay: quit command + pid ---
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start on $port"

out=""
if HUSH_PORT="$port" sh "$guard" >"$test_home/guard.out" 2>&1; then
    fail "guard passed with a live relay on $port"
fi
out=$(cat "$test_home/guard.out")
echo "$out" | grep -q -- '--quit' || fail "guard hides the quit command"
echo "$out" | grep -q "$port" || fail "guard hides the port"
echo "$out" | grep -q "$pid" || fail "guard hides the owner pid ($pid)"

# --- guard passes again once the relay is quit ---
"$bin" --quit "$port" >/dev/null 2>&1 || fail "--quit failed"
wait_down "$pid" || fail "--quit left the relay running"
wait "$pid" 2>/dev/null || true
pid=""
HUSH_PORT="$port" sh "$guard" || fail "guard still trips after --quit"

# --- P2a: pidfile path needs no curl (live relay names its pid) ---
# Without curl the ps+probe fallback is unreachable, so only the pidfile
# read (guard line 74) can name the owner. Reverting that read to
# `read -r owner` makes this fail: the 3-field line is not a bare pid,
# the fallback exits 2 without curl, and the pid is never named.
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start on $port"
out=""
if PATH="$no_curl_path" HUSH_PORT="$port" sh "$guard" >"$test_home/guard-nocurl.out" 2>&1; then
    fail "guard passed with a live relay and no curl"
fi
out=$(cat "$test_home/guard-nocurl.out")
echo "$out" | grep -q -- '--quit' || fail "guard hides the quit command without curl"
echo "$out" | grep -q "$pid" || fail "guard hid the owner pid ($pid) without curl"
"$bin" --quit "$port" >/dev/null 2>&1 || fail "--quit failed"
wait_down "$pid" || fail "--quit left the relay running"
wait "$pid" 2>/dev/null || true
pid=""

# --- P1b: live relay, hidden pidfile, no curl refuses loudly (exit 2) ---
# The guard sees a hush-relay process but cannot probe the port, so it
# must fail instead of passing over a possibly live hive.
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start on $port"
hidden_code=0
XDG_RUNTIME_DIR="$test_home/empty-run" PATH="$no_curl_path" HUSH_PORT="$port" \
    sh "$guard" >"$test_home/guard-hidden.out" 2>&1 || hidden_code=$?
test "$hidden_code" -eq 2 \
    || fail "guard must exit 2 with a live relay, hidden pidfile, no curl (got $hidden_code)"
grep -q 'curl missing' "$test_home/guard-hidden.out" \
    || fail "guard hid the curl message"
"$bin" --quit "$port" >/dev/null 2>&1 || fail "--quit failed"
wait_down "$pid" || fail "--quit left the relay running"
wait "$pid" 2>/dev/null || true
pid=""

# --- N1: a relay on another port still blocks a curl-less build (exit 2) ---
# Without the probe the guard cannot tell which port a live relay owns,
# so any hush-relay process plus no curl refuses, even for a free port
# with no pidfile. Uses its own port Q far from the other fixtures.
n1_port=$((port + 31))
"$bin" --no-open "$n1_port" >"$log" 2>&1 &
n1_pid=$!
wait_up "$n1_port" || fail "N1 relay did not start on $n1_port"
n1_code=0
PATH="$no_curl_path" HUSH_PORT="$port" \
    sh "$guard" >"$test_home/guard-n1.out" 2>&1 || n1_code=$?
test "$n1_code" -eq 2 \
    || fail "guard must exit 2 for a free port with a relay elsewhere and no curl (got $n1_code)"
grep -q 'curl missing' "$test_home/guard-n1.out" \
    || fail "guard hid the curl message"
"$bin" --quit "$n1_port" >/dev/null 2>&1 || fail "N1 --quit failed"
wait_down "$n1_pid" || fail "N1 relay did not stop"
wait "$n1_pid" 2>/dev/null || true
n1_pid=""

# --- kill-relay.sh reaps the CHILD turnserver named by the state pidfile ---
# comm must read "turnserver", so run a copy of sleep under that basename.
cp "$(command -v sleep)" "$test_home/turnserver"
"$test_home/turnserver" 60 >/dev/null 2>&1 < /dev/null &
child_pid=$!
kill -0 "$child_pid" 2>/dev/null || fail "child fixture did not start"
printf '%s\n' "$child_pid" >"$HUSH_STATE_DIR/turnserver.pid"

# A fake daemon turnserver outside the state dir must survive.
sleep 60 >/dev/null 2>&1 < /dev/null &
daemon_pid=$!
daemon_dir="$test_home/daemon-run"
mkdir -p "$daemon_dir"
printf '%s\n' "$daemon_pid" >"$daemon_dir/turnserver.pid"

sh "$killsh" || fail "kill script exited non-zero"
if kill -0 "$child_pid" 2>/dev/null; then
    fail "kill script left the CHILD turnserver running"
fi
child_pid=""
kill -0 "$daemon_pid" 2>/dev/null \
    || fail "kill script stopped a non-CHILD (daemon-stand-in) process"
test "$(cat "$daemon_dir/turnserver.pid")" = "$daemon_pid" \
    || fail "kill script touched a non-CHILD pidfile"
daemon_note="daemon stand-in survived"

echo "make guard ok (guard trips + passes; CHILD reaped, $daemon_note)"
