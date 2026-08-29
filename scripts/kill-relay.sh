#!/bin/sh
# kill-relay.sh — stop any running hush-relay before a rebuild or upgrade.
#
# Portable across Linux and *BSD: `ps -axo pid=,comm=` lists every process
# with its kernel command name, and only exact-name matches are killed, so
# this script never kills its own shell (comm is "sh"/"make", not
# "hush-relay").
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

exit 0
