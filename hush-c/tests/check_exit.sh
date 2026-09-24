#!/bin/sh
# Close stays up; Exit / --quit stop the process with code 0.
# --quit codes: 0 stopped, 1 nothing to stop, 2 stop failed or refused.
set -eu
cd "$(dirname "$0")/.."
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="${HUSH_HOME:-$test_home/hush}"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }
# The relay writes its pidfile under XDG_RUNTIME_DIR; keep it in the test tree.
export XDG_RUNTIME_DIR="$test_home/run"
mkdir -m 700 "$XDG_RUNTIME_DIR"

bin=./hush-relay
port=18768
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
pidfile=""
pid=""
virgin_pid=""
virgin_fake_pid=""
virgin_home=""
virgin_fake_bin=""
stop_pid=""
sym_pid=""
sym2_pid=""
noset_pid=""
sym_xdg=""
sym_home=""
sym_base=""
quit_err=""
quit_code=0

pidfile_path() {
    pf_port="${1:-$port}"
    if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
        printf '%s/hush/relay-%s.pid' "$XDG_RUNTIME_DIR" "$pf_port"
    elif [ -n "${HOME:-}" ]; then
        printf '%s/.local/state/hush/relay-%s.pid' "$HOME" "$pf_port"
    else
        return 1
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
    if [ -n "${virgin_pid:-}" ]; then
        kill "$virgin_pid" 2>/dev/null || true
        wait "$virgin_pid" 2>/dev/null || true
    fi
    if [ -n "${virgin_fake_pid:-}" ]; then
        kill "$virgin_fake_pid" 2>/dev/null || true
        wait "$virgin_fake_pid" 2>/dev/null || true
    fi
    if [ -n "${stop_pid:-}" ]; then
        kill -CONT "$stop_pid" 2>/dev/null || true
        kill "$stop_pid" 2>/dev/null || true
        wait "$stop_pid" 2>/dev/null || true
    fi
    if [ -n "${sym_pid:-}" ]; then
        kill "$sym_pid" 2>/dev/null || true
        wait "$sym_pid" 2>/dev/null || true
    fi
    if [ -n "${sym2_pid:-}" ]; then
        kill "$sym2_pid" 2>/dev/null || true
        wait "$sym2_pid" 2>/dev/null || true
    fi
    if [ -n "${noset_pid:-}" ]; then
        kill "$noset_pid" 2>/dev/null || true
        wait "$noset_pid" 2>/dev/null || true
    fi
    rm -f "$log" "${fake:-}" "${virgin_fake_bin:-}" "${quit_err:-}"
    rm -rf "$cfg" ${virgin_home:+"$virgin_home"} ${sym_base:+"$sym_base"} ${sym_home:+"$sym_home"}
}
trap cleanup EXIT

fail() { echo "exit check failed: $1" >&2; exit 1; }

wait_up() {
    up_port="${1:-$port}"
    i=0
    while [ "$i" -lt 50 ]; do
        if curl -sf "http://127.0.0.1:${up_port}/api/session" >/dev/null 2>&1; then
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
grep -q 'hush_relay_watch_app' src/hush_relay.c || fail "relay missing last-window watch"
grep -q 'g_leave_ack' src/hush_relay.c || fail "relay missing leave ack"
grep -q 'hush_relay_note_leave' src/hush_http.c || fail "http missing leave ack"
if awk '
    /static void hush_open_app_window/ { in_fn = 1 }
    in_fn && /g_saw_app[[:space:]]*=[[:space:]]*1/ { found = 1 }
    in_fn && /^static / && !/hush_open_app_window/ { in_fn = 0 }
    END { exit found ? 0 : 1 }
' src/hush_relay.c; then
    fail "launcher must not latch g_saw_app"
fi
grep -q 'hush_leave_app_alive()' src/hush_relay.c || fail "watch missing live --app latch"
"$bin" --close "$port" >/dev/null
test "$?" -eq 0 || fail "--close must exit 0"

"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "relay did not start"
pidfile=$(pidfile_path)
test -f "$pidfile" || fail "pidfile missing ($pidfile)"
grep -qx "$(printf '%s' "$pid")" "$pidfile" || fail "pidfile pid mismatch"

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
"$bin" --quit "$port" || fail "--quit must exit 0 when it stops the relay"
wait_down "$pid" || fail "--quit did not stop the process"
wait "$pid"
test "$?" -eq 0 || fail "--quit child must be code 0"
test ! -f "$(pidfile_path)" || fail "pidfile left after --quit"
pid=""

# Regression: a virgin HOME without .local/state must still get a pidfile;
# --quit exits 0 only after the relay is confirmed stopped and reaps the
# leftover --app child; --quit with no relay running must fail loudly.
virgin_home="$(mktemp -d)"
virgin_port=$((port + 1))
saved_home="$HOME"
if [ "${XDG_RUNTIME_DIR+x}" = "x" ]; then
    saved_runtime="$XDG_RUNTIME_DIR"
    saved_runtime_set=1
else
    saved_runtime_set=0
fi
HOME="$virgin_home"
unset XDG_RUNTIME_DIR
"$bin" --no-open "$virgin_port" >"$log" 2>&1 &
virgin_pid=$!
wait_up "$virgin_port" || fail "virgin-home relay did not start"
virgin_pidfile="$virgin_home/.local/state/hush/relay-$virgin_port.pid"
test -f "$virgin_pidfile" || fail "virgin-home pidfile missing ($virgin_pidfile)"
grep -qx "$(printf '%s' "$virgin_pid")" "$virgin_pidfile" \
    || fail "virgin-home pidfile pid mismatch"
virgin_fake_bin=$(mktemp)
printf '#!/bin/sh\nsleep 30\n' >"$virgin_fake_bin"
chmod +x "$virgin_fake_bin"
"$virgin_fake_bin" --class=hush-relay --app="http://127.0.0.1:${virgin_port}/" \
    >/dev/null 2>&1 &
virgin_fake_pid=$!
sleep 0.05
kill -0 "$virgin_fake_pid" 2>/dev/null || fail "virgin fake app did not start"
"$bin" --quit "$virgin_port" || fail "virgin --quit must exit 0"
wait_down "$virgin_pid" || fail "virgin --quit did not stop the process"
wait "$virgin_pid"
test "$?" -eq 0 || fail "virgin relay must be code 0"
test ! -f "$virgin_pidfile" || fail "virgin pidfile left after --quit"
if kill -0 "$virgin_fake_pid" 2>/dev/null; then
    fail "virgin --quit left a --app child running"
fi
virgin_fake_pid=""
virgin_pid=""
if "$bin" --quit "$virgin_port" 2>/dev/null; then
    fail "--quit with no relay must fail"
fi
HOME="$saved_home"
if [ "$saved_runtime_set" = "1" ]; then
    export XDG_RUNTIME_DIR="$saved_runtime"
else
    unset XDG_RUNTIME_DIR
fi
rm -rf "$virgin_home" "$virgin_fake_bin"
virgin_home=""
virgin_fake_bin=""

# --quit must refuse pidfiles it cannot prove are a hush-relay, and must
# report stale, corrupt, and reserved pids honestly. The planted process
# is never signalled. Ports here have no relay running.
refuse_port=$((port + 3))
refuse_dir="$XDG_RUNTIME_DIR/hush"
mkdir -p "$refuse_dir"
refuse_pidfile="$refuse_dir/relay-$refuse_port.pid"
quit_err=$(mktemp)

# Foreign live pid: sleep is not hush-relay. Exit 2, process untouched,
# pidfile left alone (not proven stale).
sleep 60 &
foreign_pid=$!
printf '%s\n' "$foreign_pid" >"$refuse_pidfile"
quit_code=0
"$bin" --quit "$refuse_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 2 || fail "foreign-pid quit must exit 2 (got $quit_code)"
kill -0 "$foreign_pid" 2>/dev/null || fail "quit signalled a foreign process"
test -f "$refuse_pidfile" || fail "quit removed an unproven pidfile"
grep -q 'not a verified hush-relay' "$quit_err" || fail "foreign-pid message wrong"
kill "$foreign_pid" 2>/dev/null || true
wait "$foreign_pid" 2>/dev/null || true

# Stale pid: already dead. Exit 1, pidfile removed.
sh -c 'exit 0' &
stale_pid=$!
wait "$stale_pid" 2>/dev/null || true
printf '%s\n' "$stale_pid" >"$refuse_pidfile"
quit_code=0
"$bin" --quit "$refuse_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 1 || fail "stale-pid quit must exit 1 (got $quit_code)"
test ! -f "$refuse_pidfile" || fail "stale pidfile not removed"
grep -q 'stale pid' "$quit_err" || fail "stale-pid message wrong"

# Reserved and corrupt contents: never signalled, always non-zero.
for content in 1 0 -5 abc 123abc '' '99999999999999999999'; do
    printf '%s\n' "$content" >"$refuse_pidfile"
    quit_code=0
    "$bin" --quit "$refuse_port" 2>"$quit_err" || quit_code=$?
    test "$quit_code" -ne 0 || fail "quit accepted pidfile content '$content'"
done
printf '1\n' >"$refuse_pidfile"
quit_code=0
"$bin" --quit "$refuse_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 2 || fail "pid-1 quit must exit 2 (got $quit_code)"
grep -q 'not a verified hush-relay' "$quit_err" || fail "pid-1 message wrong"
printf 'abc\n' >"$refuse_pidfile"
quit_code=0
"$bin" --quit "$refuse_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 2 || fail "garbage quit must exit 2 (got $quit_code)"
grep -q 'unreadable pidfile' "$quit_err" || fail "garbage message wrong"
rm -f "$refuse_pidfile"

# A relay frozen with SIGSTOP cannot die on SIGTERM: --quit waits the full
# owner budget, exits 2, and leaves pid and pidfile alone. SIGCONT then
# lets the pending SIGTERM land and the relay exits cleanly.
stop_port=$((port + 5))
"$bin" --no-open "$stop_port" >"$log" 2>&1 &
stop_pid=$!
wait_up "$stop_port" || fail "stop-test relay did not start"
stop_pidfile="$XDG_RUNTIME_DIR/hush/relay-$stop_port.pid"
test -f "$stop_pidfile" || fail "stop-test pidfile missing"
kill -STOP "$stop_pid" 2>/dev/null || fail "SIGSTOP failed"
quit_code=0
"$bin" --quit "$stop_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 2 || fail "frozen relay quit must exit 2 (got $quit_code)"
kill -0 "$stop_pid" 2>/dev/null || fail "frozen relay died unexpectedly"
test -f "$stop_pidfile" || fail "quit removed a live owner's pidfile"
grep -q 'cannot stop' "$quit_err" || fail "frozen quit message wrong"
kill -CONT "$stop_pid" 2>/dev/null || true
wait_down "$stop_pid" || fail "continued relay did not exit"
wait "$stop_pid" 2>/dev/null || true
test ! -f "$stop_pidfile" || fail "pidfile left after continued exit"
stop_pid=""

# Symlinked pidfile file or dir: the write path must refuse loudly while
# the relay still serves. Cleanup is via token /api/exit (no pidfile).
sym_port=$((port + 7))
sym_base="$(mktemp -d)"
sym_xdg="$sym_base/run"
mkdir -p "$sym_xdg"
sym_home="$(mktemp -d)"
mkdir -p "$sym_xdg/hush"
sym_target="$sym_home/link-target"
ln -s "$sym_target" "$sym_xdg/hush/relay-$sym_port.pid"
HOME="$sym_home" XDG_RUNTIME_DIR="$sym_xdg" "$bin" --no-open "$sym_port" >"$log" 2>&1 &
sym_pid=$!
wait_up "$sym_port" || fail "symlink-file relay did not start (must serve anyway)"
grep -q 'no pidfile for port' "$log" || fail "symlink-file write was silent"
test -L "$sym_xdg/hush/relay-$sym_port.pid" || fail "symlink pidfile disturbed"
test ! -e "$sym_target" || fail "symlink target created"
curl -sf -X POST "http://127.0.0.1:${sym_port}/api/exit" \
    -H 'Content-Type: application/json' -d '{}' >/dev/null \
    || fail "symlink-file exit failed"
wait_down "$sym_pid" || fail "symlink-file relay did not stop"
wait "$sym_pid" 2>/dev/null || true
sym_pid=""

sym2_port=$((port + 9))
rm -rf "$sym_xdg/hush"
ln -s "$sym_home/elsewhere" "$sym_xdg/hush"
HOME="$sym_home" XDG_RUNTIME_DIR="$sym_xdg" "$bin" --no-open "$sym2_port" >"$log" 2>&1 &
sym2_pid=$!
wait_up "$sym2_port" || fail "symlink-dir relay did not start (must serve anyway)"
grep -q 'no pidfile for port' "$log" || fail "symlink-dir write was silent"
test -L "$sym_xdg/hush" || fail "symlink dir disturbed"
curl -sf -X POST "http://127.0.0.1:${sym2_port}/api/exit" \
    -H 'Content-Type: application/json' -d '{}' >/dev/null \
    || fail "symlink-dir exit failed"
wait_down "$sym2_pid" || fail "symlink-dir relay did not stop"
wait "$sym2_pid" 2>/dev/null || true
sym2_pid=""

# No HOME and no XDG_RUNTIME_DIR: no pidfile fallback remains. The relay
# must warn loudly, still serve, and --quit must report nothing to stop.
noset_port=$((port + 11))
env -u XDG_RUNTIME_DIR -u HOME "$bin" --no-open "$noset_port" >"$log" 2>&1 &
noset_pid=$!
wait_up "$noset_port" || fail "no-env relay did not start (must serve anyway)"
grep -q 'no pidfile for port' "$log" || fail "no-env write was silent"
quit_code=0
"$bin" --quit "$noset_port" 2>"$quit_err" || quit_code=$?
test "$quit_code" -eq 1 || fail "no-env quit must exit 1 (got $quit_code)"
curl -sf -X POST "http://127.0.0.1:${noset_port}/api/exit" \
    -H 'Content-Type: application/json' -d '{}' >/dev/null \
    || fail "no-env exit failed"
wait_down "$noset_pid" || fail "no-env relay did not stop"
wait "$noset_pid" 2>/dev/null || true
noset_pid=""

HOME="$saved_home"
if [ "$saved_runtime_set" = "1" ]; then
    export XDG_RUNTIME_DIR="$saved_runtime"
else
    unset XDG_RUNTIME_DIR
fi

# Launch must not treat the dying launcher fork as last-window-gone.
stubs=$(mktemp -d)
zenlog="$stubs/zenity.log"
printf '#!/bin/sh\necho ZENITY_RAN >> "%s"\necho "Close the window"\nexit 1\n' \
    "$zenlog" >"$stubs/zenity"
chmod +x "$stubs/zenity"
PATH="$stubs" "$bin" --open "$port" >"$log" 2>&1 &
pid=$!
wait_up || fail "open start failed"
i=0
while [ "$i" -lt 6 ]; do
    sleep 0.05
    i=$((i + 1))
done
if [ -s "$zenlog" ]; then
    fail "launch invoked zenity"
fi
"$bin" --quit "$port" || fail "open --quit must exit 0"
wait_down "$pid" || fail "open quit did not stop"
wait "$pid" || true
pid=""
rm -rf "$stubs"

echo "exit routes ok"
