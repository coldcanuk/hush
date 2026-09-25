#!/bin/sh
# check_stop_relays.sh — #221: `make install` / `make clean` stop ANY live
# hush-relay* of this uid (renamed copies, "(deleted)" binaries, any port),
# close only Hush app windows, remove only stale hush-relay-* copies from
# BINDIR, and never touch non-Hush processes, other users, or ~/.hush.
#
# Drives the real top-level Makefile targets (stop-relays, install with a
# temp PREFIX, clean-relays with a temp PREFIX/BINDIR) against real relay
# copies on random ports with an isolated HOME / XDG_RUNTIME_DIR /
# HUSH_HOME, plus decoys (copied sleep, argv[0] spoof, a script named
# hush-relay-*, another uid's process) and fake browsers (copied sh named
# brave / bwrap). Linux only (/proc); skips elsewhere. Never runs a real
# browser and never uses the operator's hive, state, or prefix.
set -eu
cd "$(dirname "$0")/.."

root=$(cd .. && pwd)
killsh="$root/scripts/kill-relay.sh"
topmk="$root/Makefile"
bin="$(pwd)/hush-relay"

fail() { echo "stop-relays check failed: $1" >&2; exit 1; }
ok() { echo "stop-relays ok: $1"; }

[ -f "$killsh" ] || fail "kill script missing ($killsh)"
sh -n "$killsh" || fail "kill script has a syntax error"
[ -x "$bin" ] || fail "hush-relay not built ($bin)"

# --- static: install/clean stop relays; matching is never name/port/pkill ---
grep -q '^install:.*stop-relays' "$topmk" || fail "top 'install' does not depend on stop-relays"
grep -q '^clean:.*clean-relays' "$topmk" || fail "top 'clean' does not depend on clean-relays"
if grep -q '^install:.*guard' "$topmk"; then
    fail "top 'install' still refuses via the port guard"
fi
code=$(grep -v '^[[:space:]]*#' "$killsh")
if echo "$code" | grep -Eq '(^|[^a-z_])(pkill|pgrep|killall)([^a-z_]|$)'; then
    fail "kill script must not use pkill/pgrep/killall"
fi
# shellcheck disable=SC2016 # literal $1: the script's own readlink line.
echo "$code" | grep -q 'readlink "/proc/\$1/exe"' || fail "kill script does not match by /proc exe"

if ! readlink /proc/self/exe >/dev/null 2>&1; then
    echo "skip: check_stop_relays needs Linux /proc"
    exit 0
fi
command -v curl >/dev/null 2>&1 || fail "curl is required to wait for the relay"

# --- hermetic harness ---
tmp=$(mktemp -d)
foreign_dir=""
foreign_pid=""
spawned=""
export HOME="$tmp/home"
export XDG_RUNTIME_DIR="$tmp/run"
export XDG_CONFIG_HOME="$tmp/xdg-config"
export XDG_STATE_HOME="$tmp/xdg-state"
export HUSH_STATE_DIR="$tmp/state"
HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_PASS_HELPER
export HUSH_FAKE_PASS_DIR="$tmp/pass"
export HUSH_KILL_GRACE_S=2
unset DISPLAY WAYLAND_DISPLAY HUSH_PORT DESTDIR 2>/dev/null || true
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_STATE_HOME" \
    "$HUSH_STATE_DIR" "$HUSH_FAKE_PASS_DIR" "$tmp/bin" "$tmp/fb"
chmod 700 "$XDG_RUNTIME_DIR"

cleanup() {
    for p in $spawned; do
        kill -KILL "$p" 2>/dev/null || true
    done
    if [ -n "$foreign_pid" ]; then
        sudo -n kill -KILL "$foreign_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
    if [ -n "$foreign_dir" ]; then
        rm -rf "$foreign_dir"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

track() { spawned="$spawned $1"; }

# Top-level make with a clean environment: no inherited MAKEFLAGS from the
# `make test` that runs this script.
tmake() {
    env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL \
        make --no-print-directory -s -C "$root" "$@"
}

rand_hex() { od -An -N4 -tx4 /dev/urandom | tr -d ' \n'; }

# A random port in 20000-49999 that refuses connections (curl exit 7) and
# is not in the space-separated exclusion list $1.
free_port() {
    fp_i=0
    while [ "$fp_i" -lt 50 ]; do
        fp_r=$(od -An -N2 -tu2 /dev/urandom | tr -d ' \n')
        fp_p=$((20000 + fp_r % 30000))
        fp_i=$((fp_i + 1))
        case " ${1:-} " in
            *" $fp_p "*) continue ;;
        esac
        fp_rc=0
        curl -s --max-time 1 -o /dev/null "http://127.0.0.1:${fp_p}/" 2>/dev/null || fp_rc=$?
        if [ "$fp_rc" -eq 7 ]; then
            echo "$fp_p"
            return 0
        fi
    done
    return 1
}

wait_up() {
    wu_i=0
    while [ "$wu_i" -lt 200 ]; do
        if curl -sf "http://127.0.0.1:${1}/api/status" >/dev/null 2>&1; then
            return 0
        fi
        wu_i=$((wu_i + 1))
        sleep 0.05
    done
    return 1
}

# Gone = no /proc entry, or a zombie waiting to be reaped.
is_gone() {
    [ -e "/proc/$1" ] || return 0
    ig_stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 0
    ig_stat=${ig_stat##*) }
    case "$ig_stat" in
        Z*|X*) return 0 ;;
    esac
    return 1
}
is_alive() { ! is_gone "$1"; }
wait_gone() {
    wg_i=0
    while [ "$wg_i" -lt 100 ]; do
        is_gone "$1" && return 0
        wg_i=$((wg_i + 1))
        sleep 0.05
    done
    return 1
}

# start_relay EXE PORT TAG: a --no-open relay with its own HUSH_HOME and
# config dir; sets started_pid once /api/status answers.
start_relay() {
    mkdir -p "$tmp/hush-$3" "$tmp/cfg-$3"
    HUSH_HOME="$tmp/hush-$3" HUSH_CONFIG_DIR="$tmp/cfg-$3" \
        "$1" --no-open "$2" >"$tmp/relay-$3.log" 2>&1 </dev/null &
    started_pid=$!
    track "$started_pid"
    if ! wait_up "$2"; then
        cat "$tmp/relay-$3.log" >&2
        fail "relay $3 ($1) did not listen on $2"
    fi
}

# Waits until pid $1's comm starts with $2 (the fork has exec'd).
wait_comm() {
    wc_i=0
    while [ "$wc_i" -lt 100 ]; do
        wc_c=""
        read -r wc_c 2>/dev/null <"/proc/$1/comm" || wc_c=""
        case "$wc_c" in
            "$2"*) return 0 ;;
        esac
        wc_i=$((wc_i + 1))
        sleep 0.02
    done
    return 1
}

show() { sed 's/^/  | /' "$1"; }

r=$(rand_hex)
sh_bin=$(command -v sh)
sleep_bin=$(command -v sleep)
loop='while :; do sleep 1; done'

# ============ phase 1: make stop-relays (the install stop path) ============
# A: renamed copy on a random non-default port (the Chuck shape).
copy_a="$tmp/bin/hush-relay-test-$r"
cp "$bin" "$copy_a"
port_a=$(free_port "10555") || fail "no free port"
start_relay "$copy_a" "$port_a" a
pid_a=$started_pid

# B: renamed copy whose binary is deleted while it runs: exe reads
# "<path> (deleted)".
copy_b="$tmp/bin/hush-relay-gone-$r"
cp "$bin" "$copy_b"
port_b=$(free_port "10555 $port_a") || fail "no free port"
start_relay "$copy_b" "$port_b" b
pid_b=$started_pid
rm -f "$copy_b"
readlink "/proc/$pid_b/exe" | grep -q ' (deleted)$' \
    || fail "fixture: relay B exe does not read as (deleted)"

# S: a hush-relay* executable that ignores SIGTERM (copied sh), so only
# the SIGKILL escalation can stop it.
cp "$sh_bin" "$tmp/bin/hush-relay-stubborn"
"$tmp/bin/hush-relay-stubborn" -c "trap '' TERM; $loop" </dev/null >/dev/null 2>&1 &
pid_s=$!
track "$pid_s"
wait_comm "$pid_s" hush-relay-stub || fail "fixture: stubborn relay did not start"

# Decoys that must survive: "hush" in name / argv / comm, exe not hush-relay*.
cp "$sleep_bin" "$tmp/bin/hushy-sleep"
"$tmp/bin/hushy-sleep" 300 </dev/null >/dev/null 2>&1 &
pid_hushy=$!
track "$pid_hushy"
wait_comm "$pid_hushy" hushy-sleep || fail "fixture: hushy-sleep did not start"

pid_argv0=""
if command -v bash >/dev/null 2>&1; then
    # cmdline reads "hush-relay-decoy 300"; exe and comm are sleep.
    bash -c 'exec -a hush-relay-decoy sleep 300' </dev/null >/dev/null 2>&1 &
    pid_argv0=$!
    track "$pid_argv0"
    wait_comm "$pid_argv0" sleep || fail "fixture: argv0 decoy did not start"
    tr '\0' ' ' <"/proc/$pid_argv0/cmdline" | grep -q '^hush-relay-decoy 300' \
        || fail "fixture: argv0 decoy cmdline is not hush-relay-decoy"
else
    echo "skip: argv0 decoy needs bash (exec -a)"
fi

# A script named hush-relay-script: comm is hush-relay-scri, exe is sh.
printf '#!/bin/sh\n%s\n' "$loop" >"$tmp/bin/hush-relay-script"
chmod 755 "$tmp/bin/hush-relay-script"
"$tmp/bin/hush-relay-script" </dev/null >/dev/null 2>&1 &
pid_script=$!
track "$pid_script"
wait_comm "$pid_script" hush-relay-scri || fail "fixture: script decoy comm is not hush-relay-scri"

# Another uid's hush-relay-named process: never ours to signal.
if sudo -n true 2>/dev/null && id nobody >/dev/null 2>&1; then
    foreign_dir=$(mktemp -d)
    chmod 755 "$foreign_dir"
    cp "$sleep_bin" "$foreign_dir/hush-relay-foreign"
    chmod 755 "$foreign_dir/hush-relay-foreign"
    sudo -n -u nobody "$foreign_dir/hush-relay-foreign" 300 </dev/null >/dev/null 2>&1 &
    track "$!"
    i=0
    while [ "$i" -lt 100 ] && [ -z "$foreign_pid" ]; do
        foreign_pid=$(ps -u nobody -o pid=,comm= 2>/dev/null \
            | awk '$2 ~ /^hush-relay-fore/ {print $1; exit}')
        i=$((i + 1))
        [ -n "$foreign_pid" ] || sleep 0.05
    done
    [ -n "$foreign_pid" ] || fail "fixture: foreign-uid process did not start"
else
    echo "skip: foreign-uid decoy needs passwordless sudo and user nobody"
fi

out1="$tmp/stop.out"
rc1=0
tmake stop-relays >"$out1" 2>&1 || rc1=$?
echo "make stop-relays (exit $rc1):"
show "$out1"
[ "$rc1" -eq 0 ] || fail "make stop-relays exited $rc1"

wait_gone "$pid_a" || fail "renamed relay pid $pid_a ($copy_a) survived make stop-relays"
grep -qF "found hush-relay pid $pid_a exe $copy_a port $port_a (match: exe)" "$out1" \
    || fail "stop output does not report pid $pid_a, exe $copy_a, port $port_a"
grep -qF "stopped pid $pid_a $copy_a (SIG" "$out1" \
    || fail "stop output does not name stopped pid $pid_a and path $copy_a"
ok "renamed relay $copy_a (pid $pid_a, port $port_a) stopped; output names pid and path"

wait_gone "$pid_b" || fail "deleted-binary relay pid $pid_b survived"
grep -qF "stopped pid $pid_b $copy_b (deleted) (SIG" "$out1" \
    || fail "stop output does not name deleted-binary relay pid $pid_b"
ok "deleted-binary relay pid $pid_b ($copy_b (deleted), port $port_b) stopped"

wait_gone "$pid_s" || fail "TERM-ignoring relay pid $pid_s survived"
grep -qF "stopped pid $pid_s $tmp/bin/hush-relay-stubborn (SIGKILL after 2s)" "$out1" \
    || fail "stubborn relay pid $pid_s was not escalated to SIGKILL"
ok "TERM-ignoring hush-relay-stubborn pid $pid_s escalated to SIGKILL"

# assert_untouched PID LABEL OUTFILE: alive and never found/stopped/closed.
assert_untouched() {
    is_alive "$1" || fail "$2 (pid $1) was killed"
    if grep -qE "(found hush-relay pid|stopped pid|closing Hush app window pid|closed Hush app window pid) $1( |\$)" "$3"; then
        fail "$2 (pid $1) was targeted"
    fi
}
assert_untouched "$pid_hushy" "decoy hushy-sleep" "$out1"
ok "decoy hushy-sleep (pid $pid_hushy, exe $tmp/bin/hushy-sleep) survived"
if [ -n "$pid_argv0" ]; then
    assert_untouched "$pid_argv0" "argv0 decoy hush-relay-decoy" "$out1"
    ok "argv0 decoy 'hush-relay-decoy 300' (pid $pid_argv0, exe sleep) survived"
fi
assert_untouched "$pid_script" "script decoy hush-relay-script" "$out1"
ok "script decoy hush-relay-script (pid $pid_script, comm hush-relay-scri, exe sh) survived"
if [ -n "$foreign_pid" ]; then
    assert_untouched "$foreign_pid" "foreign-uid hush-relay-foreign" "$out1"
    grep -qF "note: process $foreign_pid (comm hush-relay-fore) belongs to uid $(id -u nobody)" "$out1" \
        || fail "foreign-uid relay-named process $foreign_pid was not reported as left running"
    ok "uid nobody's hush-relay-foreign (pid $foreign_pid) untouched, reported as left running"
fi

# ============ phase 2: make install PREFIX=<tmp> stops, then installs ======
if [ "$(id -u)" = "0" ]; then
    echo "skip: make install end-to-end is not run as root (would write /etc, /lib)"
else
    copy_d="$tmp/bin/hush-relay-m11-$r"
    cp "$bin" "$copy_d"
    port_d=$(free_port "10555 $port_a $port_b") || fail "no free port"
    start_relay "$copy_d" "$port_d" d
    pid_d=$started_pid
    out2="$tmp/install.out"
    rc2=0
    tmake install PREFIX="$tmp/iprefix" >"$out2" 2>&1 || rc2=$?
    echo "make install PREFIX=$tmp/iprefix (exit $rc2):"
    show "$out2"
    [ "$rc2" -eq 0 ] || fail "make install exited $rc2 with a renamed relay running"
    wait_gone "$pid_d" || fail "make install left renamed relay pid $pid_d running"
    grep -qF "stopped pid $pid_d $copy_d (SIG" "$out2" \
        || fail "make install output does not name stopped pid $pid_d and path $copy_d"
    [ -x "$tmp/iprefix/bin/hush-relay" ] || fail "make install did not install into the temp PREFIX"
    ok "make install stopped $copy_d (pid $pid_d, port $port_d), then installed $tmp/iprefix/bin/hush-relay"
    assert_untouched "$pid_hushy" "decoy hushy-sleep" "$out2"
    assert_untouched "$pid_script" "script decoy hush-relay-script" "$out2"
fi

# ============ phase 3: make clean-relays (the clean pre-step) ==============
cbin="$tmp/cprefix/bin"
mkdir -p "$cbin" "$tmp/keep"
copy_c="$cbin/hush-relay-m11-c$r"
cp "$bin" "$copy_c"
port_c=$(free_port "10555 $port_a $port_b ${port_d:-}") || fail "no free port"
start_relay "$copy_c" "$port_c" c
pid_c=$started_pid
port_x=$(free_port "10555 $port_a $port_b ${port_d:-} $port_c") || fail "no free port"

# Fake browsers: copied sh named brave / bwrap carrying flags in their own
# cmdline. Only --class=hush-relay* or the --app of a stopped relay closes.
cp "$sh_bin" "$tmp/fb/brave"
cp "$sh_bin" "$tmp/fb/bwrap"
"$tmp/fb/brave" -c "$loop" brave --class=hush-relay-test </dev/null >/dev/null 2>&1 &
pid_w1=$!
track "$pid_w1"
"$tmp/fb/bwrap" -c "$loop" bwrap "--app=http://127.0.0.1:${port_c}/" </dev/null >/dev/null 2>&1 &
pid_w2=$!
track "$pid_w2"
"$tmp/fb/brave" -c "$loop" brave --class=other </dev/null >/dev/null 2>&1 &
pid_w3=$!
track "$pid_w3"
"$tmp/fb/brave" -c "$loop" brave "--app=http://127.0.0.1:${port_x}/" </dev/null >/dev/null 2>&1 &
pid_w4=$!
track "$pid_w4"
"$sh_bin" -c "$loop" notabrowser --class=hush-relay-x </dev/null >/dev/null 2>&1 &
pid_w5=$!
track "$pid_w5"
for w in "$pid_w1" "$pid_w3" "$pid_w4"; do
    wait_comm "$w" brave || fail "fixture: fake brave $w did not start"
done
wait_comm "$pid_w2" bwrap || fail "fixture: fake bwrap did not start"

# BINDIR contents: what clean-relays may and may not remove.
cp "$bin" "$cbin/hush-relay-old"                         # stale ELF copy: removed
cp "$bin" "$tmp/keep/hush-relay-target"
ln -s "$tmp/keep/hush-relay-target" "$cbin/hush-relay-link" # symlink: kept
cp "$bin" "$cbin/hush-relay-stop"                        # helper name: kept
printf 'notes\n' >"$cbin/hush-relay-notes.txt"          # not ELF: kept
cp "$bin" "$cbin/hush-relay"                             # installed name (uninstall's job): kept
cp "$sleep_bin" "$cbin/other-tool"                       # unrelated: kept
foreign_file=""
if [ -n "$foreign_pid" ]; then
    foreign_file="$cbin/hush-relay-foreign-owned"
    cp "$bin" "$foreign_file"
    sudo -n chown nobody "$foreign_file" || fail "fixture: chown nobody failed"
fi
# Runtime data that must survive.
mkdir -p "$HOME/.hush/config"
printf 'keep\n' >"$HOME/.hush/config/sentinel"
printf 'keep\n' >"$tmp/hush-c/sentinel"

out3="$tmp/clean.out"
rc3=0
tmake clean-relays PREFIX="$tmp/cprefix" BINDIR="$cbin" >"$out3" 2>&1 || rc3=$?
echo "make clean-relays PREFIX=$tmp/cprefix (exit $rc3):"
show "$out3"
[ "$rc3" -eq 0 ] || fail "make clean-relays exited $rc3"

wait_gone "$pid_c" || fail "clean-relays left relay pid $pid_c running"
grep -qF "stopped pid $pid_c $copy_c (SIG" "$out3" \
    || fail "clean output does not name stopped pid $pid_c and path $copy_c"
ok "clean-relays stopped $copy_c (pid $pid_c, port $port_c)"

wait_gone "$pid_w1" || fail "fake brave --class=hush-relay-test (pid $pid_w1) not closed"
grep -qF "closing Hush app window pid $pid_w1 (brave) --class=hush-relay-test" "$out3" \
    || fail "clean output does not name window pid $pid_w1"
ok "fake brave --class=hush-relay-test (pid $pid_w1) closed"
wait_gone "$pid_w2" || fail "fake bwrap --app=:$port_c (pid $pid_w2) not closed"
grep -qF "closing Hush app window pid $pid_w2 (bwrap) --app=http://127.0.0.1:${port_c}/" "$out3" \
    || fail "clean output does not name window pid $pid_w2"
ok "fake bwrap --app=http://127.0.0.1:${port_c}/ (pid $pid_w2) closed"

assert_untouched "$pid_w3" "fake brave --class=other" "$out3"
grep -qF "left alone pid $pid_w3 (brave)" "$out3" \
    || fail "fake brave --class=other (pid $pid_w3) not reported as left alone"
ok "fake brave --class=other (pid $pid_w3) survived, reported left alone"
assert_untouched "$pid_w4" "fake brave --app of a non-stopped port" "$out3"
ok "fake brave --app=http://127.0.0.1:${port_x}/ (pid $pid_w4, no relay stopped there) survived"
assert_untouched "$pid_w5" "non-browser with --class=hush-relay-x" "$out3"
ok "non-browser sh with --class=hush-relay-x (pid $pid_w5) survived"

[ ! -e "$copy_c" ] || fail "stale running copy $copy_c not removed"
grep -qF "removed stale copy $copy_c" "$out3" || fail "removal of $copy_c not printed"
[ ! -e "$cbin/hush-relay-old" ] || fail "stale copy hush-relay-old not removed"
grep -qF "removed stale copy $cbin/hush-relay-old" "$out3" || fail "removal of hush-relay-old not printed"
ok "stale copies $copy_c and $cbin/hush-relay-old removed and printed"
[ -L "$cbin/hush-relay-link" ] || fail "symlink hush-relay-link was removed"
[ -f "$tmp/keep/hush-relay-target" ] || fail "symlink target was removed (followed)"
[ -f "$cbin/hush-relay-stop" ] || fail "helper name hush-relay-stop was removed"
[ -f "$cbin/hush-relay-notes.txt" ] || fail "non-ELF hush-relay-notes.txt was removed"
[ -f "$cbin/hush-relay" ] || fail "clean-relays removed the installed hush-relay (uninstall's job)"
[ -f "$cbin/other-tool" ] || fail "unrelated other-tool was removed"
if [ -n "$foreign_file" ]; then
    [ -f "$foreign_file" ] || fail "file owned by nobody was removed"
fi
ok "kept: symlink (target intact), hush-relay-stop, non-ELF, installed hush-relay, other-tool${foreign_file:+, nobody-owned copy}"
[ "$(cat "$HOME/.hush/config/sentinel")" = "keep" ] || fail "HOME/.hush data touched"
[ "$(cat "$tmp/hush-c/sentinel")" = "keep" ] || fail "HUSH_HOME data touched"
ok "HOME/.hush and HUSH_HOME sentinels untouched"
assert_untouched "$pid_hushy" "decoy hushy-sleep" "$out3"
assert_untouched "$pid_script" "script decoy hush-relay-script" "$out3"

# ============ phase 4: nothing running is a clean exit 0 ===================
out4="$tmp/none.out"
rc4=0
tmake stop-relays >"$out4" 2>&1 || rc4=$?
show "$out4"
[ "$rc4" -eq 0 ] || fail "make stop-relays with nothing running exited $rc4"
grep -qF "no running hush-relay* process for uid $(id -u)" "$out4" \
    || fail "idle stop did not say nothing was running"
ok "idle make stop-relays exits 0"

echo "stop-relays check ok (renamed/deleted/stubborn relays stopped; decoys, other uid, non-Hush windows and files survived)"
