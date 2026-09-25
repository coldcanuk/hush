#!/bin/sh
# check-relay-port.sh — fail-fast rebuild guard for `make` / `make install`.
#
# Fails (exit 1) when a live hush-relay owns the port, printing the pid
# and the quit command. Passes (exit 0) when no relay owns the port: with
# curl, a free port passes even if a relay runs on another port; without
# curl, any running hush-relay fails the guard (exit 2) instead of guessing.
# Never kills anything; `make clean` (scripts/kill-relay.sh) is the killer.
#
# Port: $1, else $HUSH_PORT, else 10555 (HUSH_DEFAULT_PORT in
# hush-c/include/hush_relay.h). Usage: sh scripts/check-relay-port.sh [port]
#
# Ownership is resolved exactly like the relay resolves its pidfile
# (hush_pidfile_dir in hush-c/src/hush_relay.c: absolute
# XDG_RUNTIME_DIR/hush, else absolute HOME/.local/state/hush; no /tmp
# fallback; file relay-<port>.pid holds "pid starttime port", older files
# pid only). A live pid in that file owns the port. Without a live pidfile,
# a probe of http://127.0.0.1:<port>/api/status plus a live hush-relay
# process also counts (covers a relay started under a different XDG/HOME).
# The probe needs curl, but only when a hush-relay process is actually
# running: with no relay at all the guard passes even without curl; with
# a live relay, no pidfile owner, and no curl it fails (exit 2) instead of guessing.

set -eu

port="${1:-${HUSH_PORT:-10555}}"
case "$port" in
    ''|*[!0-9]*) echo "check-relay-port: bad port '$port'" >&2; exit 2 ;;
esac

pidfile_dir() {
    # Mirrors hush_pidfile_dir: absolute paths only, no /tmp fallback.
    # Fails (non-zero) when neither variable gives an absolute dir.
    case "${XDG_RUNTIME_DIR:-}" in
        /*) printf '%s/hush' "$XDG_RUNTIME_DIR"; return 0 ;;
    esac
    case "${HOME:-}" in
        /*) printf '%s/.local/state/hush' "$HOME"; return 0 ;;
    esac
    return 1
}

comm_of() {
    # Prints the kernel command name for $1, or nothing when gone.
    ps -p "$1" -o comm= 2>/dev/null | tr -d ' ' || true
}

pid_alive() {
    kill -0 "$1" 2>/dev/null
}

relay_pids() {
    ps -axo pid=,comm= 2>/dev/null | awk '$2 == "hush-relay" {print $1}' || true
}

port_answers() {
    # Any 2xx from the unauthenticated status probe means a relay is home.
    # The caller guarantees curl exists before taking the probe path.
    curl -sf --max-time 2 "http://127.0.0.1:${port}/api/status" \
        >/dev/null 2>&1
}

owner=""
dir=""
dir=$(pidfile_dir) || dir=""
pidfile=""
if [ -n "$dir" ]; then
    pidfile="${dir}/relay-${port}.pid"
fi
if [ -n "$pidfile" ] && [ -f "$pidfile" ]; then
    # Pidfile holds "pid starttime port" (older files: pid only); the
    # owner is always the first field.
    # shellcheck disable=SC2162,SC2034: fixed field layout; _rest unused.
    read -r owner _rest <"$pidfile" 2>/dev/null || owner=""
    case "$owner" in
        ''|*[!0-9]*) owner="" ;;
        *) pid_alive "$owner" || owner="" ;;
    esac
    if [ -n "$owner" ] && [ "$(comm_of "$owner")" != "hush-relay" ]; then
        # Stale pidfile reused by an unrelated process: not our owner.
        owner=""
    fi
fi

if [ -z "$owner" ]; then
    pids=$(relay_pids)
    if [ -n "$pids" ]; then
        # A relay process exists but no pidfile names it: only the port
        # probe can attribute it, so curl is required here (and only here).
        if ! command -v curl >/dev/null 2>&1; then
            echo "check-relay-port: hush-relay is running but its port cannot be probed (curl missing)" >&2
            exit 2
        fi
        if port_answers; then
            # Attribute the port to the first live hush-relay for the message.
            for p in $pids; do
                if pid_alive "$p"; then
                    owner="$p"
                    break
                fi
            done
        fi
    fi
fi

if [ -n "$owner" ]; then
    cat >&2 <<EOF
hush-relay is running on port ${port} (pid ${owner}); refusing to build over a live hive.
Stop it first, then rebuild:
  hush-relay --quit ${port}   # pid ${owner}; Exit in the hive works too
Close only dismisses the window — the hive keeps the port. See README "Close vs Exit".
EOF
    exit 1
fi

exit 0
