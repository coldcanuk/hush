#!/bin/sh
# kill-relay.sh — stop any running hush-relay before a rebuild or upgrade.
#
# Portable across Linux and *BSD: `ps -axo pid=,comm=` lists every process
# with its kernel command name, and only exact-name matches are killed, so
# this script never kills its own shell (comm is "sh"/"make", not
# "hush-relay").
#
# Also reaps the CHILD-mode turnserver the relay forked (pid in
# $HUSH_STATE_DIR/turnserver.pid, else $XDG_STATE_HOME/hush, else
# $HOME/.local/state/hush, else /tmp/hush — the same resolution as
# hush_turn_resolve_state in hush-c/src/hush_turn.c). Only that pidfile
# target is ever signalled: the systemd daemon (hush-turn.service,
# /run/hush-turn/turnserver.pid, /etc/hush/turnserver.conf) is never
# touched — no systemctl, no broad turnserver match.
#
# Always exits 0 so a `make clean` or a package maintainer script never
# fails merely because nothing was running.

pids=$(ps -axo pid=,comm= 2>/dev/null | awk '$2 == "hush-relay" {print $1}')

if [ -n "$pids" ]; then
    # SIGTERM first so the relay can reap its agent jobs, then a short grace
    # period, then force anything still alive.
    kill $pids 2>/dev/null || true
    sleep 1
    for p in $pids; do
        if kill -0 "$p" 2>/dev/null; then
            kill -KILL "$p" 2>/dev/null || true
        fi
    done
fi

# CHILD-mode turnserver only: kill the pid named by the relay-owned state
# dir pidfile. A stale pidfile reused by a non-turnserver process is left
# alone (comm check), and a missing pidfile means nothing to do.
state_dir=""
if [ -n "${HUSH_STATE_DIR:-}" ]; then
    state_dir="$HUSH_STATE_DIR"
elif [ -n "${XDG_STATE_HOME:-}" ]; then
    state_dir="$XDG_STATE_HOME/hush"
elif [ -n "${HOME:-}" ]; then
    state_dir="$HOME/.local/state/hush"
else
    state_dir="/tmp/hush"
fi
turn_pidfile="$state_dir/turnserver.pid"
if [ -f "$turn_pidfile" ]; then
    # shellcheck disable=SC2162: pidfile holds one pid by construction.
    read -r turn_pid <"$turn_pidfile" 2>/dev/null || turn_pid=""
    case "$turn_pid" in
        ''|*[!0-9]*) turn_pid="" ;;
        *)
            if ! kill -0 "$turn_pid" 2>/dev/null; then
                turn_pid=""
            elif [ "$(ps -p "$turn_pid" -o comm= 2>/dev/null | tr -d ' ')" != "turnserver" ]; then
                turn_pid=""
            fi
            ;;
    esac
    if [ -n "$turn_pid" ]; then
        kill "$turn_pid" 2>/dev/null || true
        sleep 1
        if kill -0 "$turn_pid" 2>/dev/null; then
            kill -KILL "$turn_pid" 2>/dev/null || true
        fi
    fi
fi

exit 0
