#!/bin/sh
# check-relay-port.sh — fail-fast rebuild guard for `make` / `make install`.
#
# Fails (exit 1) when a live hush-relay owns the default port, printing the
# pid and the quit command. Passes (exit 0) when the port is free.
# Never kills anything; `make clean` (scripts/kill-relay.sh) is the killer.
#
# Port: $1, else $HUSH_PORT, else 10555 (HUSH_DEFAULT_PORT in
# hush-c/include/hush_relay.h). Usage: sh scripts/check-relay-port.sh [port]
#
# Ownership is resolved exactly like the relay resolves its pidfile
# (hush-c/src/hush_relay.c: XDG_RUNTIME_DIR/hush, then
# HOME/.local/state/hush, then /tmp/hush, file relay-<port>.pid). A live
# pid in that file owns the port. Without a live pidfile, a probe of
# http://127.0.0.1:<port>/api/status plus a live hush-relay process also
# counts (covers a relay started under a different XDG/HOME).

set -eu

port="${1:-${HUSH_PORT:-10555}}"
case "$port" in
    ''|*[!0-9]*) echo "check-relay-port: bad port '$port'" >&2; exit 2 ;;
esac

pidfile_dir() {
    if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
        printf '%s/hush' "$XDG_RUNTIME_DIR"
    elif [ -n "${HOME:-}" ]; then
        printf '%s/.local/state/hush' "$HOME"
    else
        printf '/tmp/hush'
    fi
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
    if command -v curl >/dev/null 2>&1; then
        curl -sf --max-time 2 "http://127.0.0.1:${port}/api/status" \
            >/dev/null 2>&1
    else
        return 1
    fi
}

owner=""
dir=$(pidfile_dir)
pidfile="${dir}/relay-${port}.pid"
if [ -f "$pidfile" ]; then
    # shellcheck disable=SC2162: pidfile holds one pid by construction.
    read -r owner <"$pidfile" 2>/dev/null || owner=""
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
    if [ -n "$pids" ] && port_answers; then
        # Attribute the port to the first live hush-relay for the message.
        for p in $pids; do
            if pid_alive "$p"; then
                owner="$p"
                break
            fi
        done
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
