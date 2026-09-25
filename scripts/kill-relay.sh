#!/bin/sh
# kill-relay.sh — stop every running hush-relay owned by this user before
# `make install` / `make clean`, renamed copies included (#221).
#
# Usage:
#   sh scripts/kill-relay.sh [stop]        # make install (via stop-relays)
#   sh scripts/kill-relay.sh clean BINDIR  # make clean (via clean-relays)
#
# stop (default)
#   Finds every live process of the CURRENT uid whose executable basename
#   starts with "hush-relay": readlink /proc/PID/exe with a " (deleted)"
#   suffix stripped, so hush-relay, hush-relay-m11-e2596637 and a replaced
#   binary all match. /proc/PID/comm is only a secondary check, used when
#   the exe link is unreadable. Never matched by exact name, by port, or
#   by cmdline text (a process whose argv merely mentions "hush" is left
#   alone), and never this script's own shell or its parent make.
#   Each match gets SIGTERM, then SIGKILL after HUSH_KILL_GRACE_S seconds
#   (default 3); identity is re-checked before every signal so a reused
#   pid is never hit. Every pid and path stopped is printed. Also reaps
#   the relay's CHILD-mode turnserver (unchanged, see below).
#   Exit 0 when nothing ran or everything stopped; exit 1 when any relay
#   survived SIGKILL (make then stops before installing over it).
#
# clean
#   stop, then close Hush app windows, then remove stale renamed
#   hush-relay-* copies from BINDIR. Never touches ~/.hush or HUSH_HOME.
#   Windows: only user-owned Chromium-family / bwrap processes (the
#   browsers hush-relay launches) whose OWN cmdline has an argument
#   starting with --class=hush-relay, or exactly
#   --app=http://127.0.0.1:<port of a relay just stopped>/. A Hush --app
#   window that a browser handed off into an already-running shared
#   browser process lives in a process without those flags; closing it
#   would kill the user's whole browser, so such processes are only
#   reported as "left alone", never signalled.
#   Files: only regular files (never symlinks, never followed) named
#   hush-relay-* directly in BINDIR, owned by this uid, starting with the
#   ELF magic. hush-relay-stop (the packaging helper, shipped to
#   share/hush, not BINDIR) is always kept.
#
# Why SIGTERM and not `<exe> --quit`: --quit is not PID-scoped. It reads
# the pidfile relay-<port>.pid under the CALLER's $XDG_RUNTIME_DIR/hush
# (else $HOME/.local/state/hush) and, after an identity check, sends that
# pid SIGTERM (hush_relay_quit / hush_pid_stop_timed in
# hush-c/src/hush_relay.c), which is the same signal sent here. It cannot
# reach a relay whose pidfile lives under another XDG/HOME, and it refuses
# off Linux. Running a stale renamed binary is worse: older builds'
# --quit (e.g. e2596637) signal whatever pid the pidfile holds without
# the starttime identity check, so exec'ing an unknown old binary could
# signal a reused, unrelated pid. SIGTERM by verified exe is both
# narrower and equivalent for a current relay.
#
# CHILD-mode turnserver: the pid in $HUSH_STATE_DIR/turnserver.pid, else
# $XDG_STATE_HOME/hush, else $HOME/.local/state/hush, else /tmp/hush — the
# same resolution as hush_turn_resolve_state in hush-c/src/hush_turn.c.
# Only that pidfile target is ever signalled: the systemd daemon
# (hush-turn.service, /run/hush-turn/turnserver.pid,
# /etc/hush/turnserver.conf) is never touched — no systemctl, no broad
# turnserver match.
#
# Without /proc (*BSD) the relay scan falls back to `ps` comm (prefix
# hush-relay, current uid only) and window closing is skipped.

set -u

me="kill-relay"
uid=$(id -u)
self=$$
parent=${PPID:-0}
grace="${HUSH_KILL_GRACE_S:-3}"
case "$grace" in
    ''|*[!0-9]*) grace=3 ;;
esac
if [ "$grace" -lt 1 ]; then
    grace=1
fi

have_proc=0
if readlink "/proc/$self/exe" >/dev/null 2>&1; then
    have_proc=1
fi

rc=0
stopped_ports=""

# --- identity helpers -------------------------------------------------

# Sets pu_uid to the real uid of pid $1 (Uid: real effective saved fs),
# empty when gone. No subshell: runs once per process in every scan.
proc_uid() {
    pu_uid=""
    if [ "$have_proc" = 1 ]; then
        while read -r pu_key pu_real _; do
            if [ "$pu_key" = "Uid:" ]; then
                pu_uid=$pu_real
                break
            fi
        done 2>/dev/null <"/proc/$1/status"
    else
        pu_uid=$(ps -o uid= -p "$1" 2>/dev/null | tr -d ' ')
    fi
}

# True while pid $1 exists and is not a zombie.
alive() {
    kill -0 "$1" 2>/dev/null || return 1
    if [ "$have_proc" = 1 ]; then
        al_stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
        # State is the first field after the LAST ") " (comm may hold ")").
        al_state=${al_stat##*) }
        al_state=${al_state%% *}
    else
        al_state=$(ps -o stat= -p "$1" 2>/dev/null | tr -d ' ')
    fi
    case "$al_state" in
        ''|Z*|X*) return 1 ;;
    esac
    return 0
}

# Kernel command name of pid $1 (secondary check only).
proc_comm() {
    pc_comm=""
    if [ "$have_proc" = 1 ]; then
        read -r pc_comm 2>/dev/null <"/proc/$1/comm" || pc_comm=""
    else
        pc_comm=$(ps -o comm= -p "$1" 2>/dev/null)
        pc_comm=${pc_comm##*/}
    fi
    printf '%s' "$pc_comm"
}

# Sets m_path (as the kernel reports it) and m_how when pid $1 is a live
# hush-relay* owned by this uid; returns 1 otherwise.
relay_match() {
    m_path=""
    m_how=""
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    if [ "$1" = "$self" ] || [ "$1" = "$parent" ] || [ "$1" -le 1 ]; then
        return 1
    fi
    proc_uid "$1"
    [ "$pu_uid" = "$uid" ] || return 1
    alive "$1" || return 1
    if [ "$have_proc" = 1 ]; then
        rm_raw=$(readlink "/proc/$1/exe" 2>/dev/null) || rm_raw=""
        if [ -n "$rm_raw" ]; then
            rm_exe=${rm_raw%" (deleted)"}
            case "${rm_exe##*/}" in
                hush-relay*)
                    m_path=$rm_raw
                    m_how="exe"
                    return 0
                    ;;
            esac
            # A readable exe that is not hush-relay* is final: a script or
            # an argv[0] trick named hush-relay-* is not a relay.
            return 1
        fi
    fi
    rm_comm=$(proc_comm "$1")
    case "$rm_comm" in
        hush-relay*)
            m_path="(exe unreadable; comm $rm_comm)"
            m_how="comm"
            return 0
            ;;
    esac
    return 1
}

# Listen port of relay pid $1, parsed like hush_parse_args in
# hush-c/src/hush_relay_main.c: the last non-option argument, skipping the
# value of a split `--listen ADDR`; none means HUSH_DEFAULT_PORT (10555).
# Prints nothing when the port cannot be parsed.
relay_port() {
    if [ "$have_proc" != 1 ]; then
        return 0
    fi
    tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | awk '
        NR == 1 { next }
        skip { skip = 0; next }
        $0 == "--listen" { skip = 1; next }
        /^-/ { next }
        { port = $0; seen = 1 }
        END {
            if (!seen) { print 10555; exit }
            if (port ~ /^[0-9]+$/ && port + 0 > 0 && port + 0 <= 65535)
                print port + 0
        }'
}

scan_relays() {
    if [ "$have_proc" = 1 ]; then
        for sr_dir in /proc/[0-9]*; do
            sr_pid=${sr_dir#/proc/}
            if relay_match "$sr_pid"; then
                printf '%s\n' "$sr_pid"
            fi
        done
    else
        ps -ax -o pid= -o uid= -o comm= 2>/dev/null | awk -v u="$uid" \
            -v s="$self" -v pp="$parent" '
            $2 == u && $1 != s && $1 != pp {
                n = split($3, parts, "/")
                if (index(parts[n], "hush-relay") == 1) print $1
            }'
    fi
}

# --- stop -------------------------------------------------------------

stop_relays() {
    sr_pids=$(scan_relays)
    if [ -z "$sr_pids" ]; then
        echo "$me: no running hush-relay* process for uid $uid"
        return 0
    fi
    sr_table=""
    for p in $sr_pids; do
        relay_match "$p" || continue
        port=$(relay_port "$p")
        echo "$me: found hush-relay pid $p exe $m_path port ${port:-unknown} (match: $m_how)"
        if [ -n "$port" ]; then
            stopped_ports="$stopped_ports $port"
        fi
        sr_table="$sr_table$p $m_path
"
        kill -TERM "$p" 2>/dev/null || true
    done

    sr_tries=$((grace * 5))
    sr_i=0
    while [ "$sr_i" -lt "$sr_tries" ]; do
        sr_left=0
        for p in $sr_pids; do
            if relay_match "$p"; then
                sr_left=1
                break
            fi
        done
        [ "$sr_left" = 0 ] && break
        sleep 0.2
        sr_i=$((sr_i + 1))
    done

    sr_status=0
    sr_rest=$sr_table
    while [ -n "$sr_rest" ]; do
        sr_line=${sr_rest%%
*}
        sr_rest=${sr_rest#*
}
        p=${sr_line%% *}
        path=${sr_line#* }
        [ -n "$p" ] || continue
        if ! relay_match "$p" || [ "$m_path" != "$path" ]; then
            echo "$me: stopped pid $p $path (SIGTERM)"
            continue
        fi
        kill -KILL "$p" 2>/dev/null || true
        sleep 0.2
        if relay_match "$p" && [ "$m_path" = "$path" ]; then
            echo "$me: FAILED to stop pid $p $path (alive after SIGKILL)" >&2
            sr_status=1
        else
            echo "$me: stopped pid $p $path (SIGKILL after ${grace}s)"
        fi
    done
    return "$sr_status"
}

# Other users' relays are never signalled; name them so an operator
# running `sudo make install` knows a user relay still serves the old
# binary. comm is world-readable; nothing here sends a signal.
note_foreign_relays() {
    [ "$have_proc" = 1 ] || return 0
    for nf_dir in /proc/[0-9]*; do
        nf_comm=""
        read -r nf_comm 2>/dev/null <"$nf_dir/comm" || continue
        case "$nf_comm" in
            hush-relay*) ;;
            *) continue ;;
        esac
        nf_uid=""
        while read -r nf_key nf_real _; do
            if [ "$nf_key" = "Uid:" ]; then
                nf_uid=$nf_real
                break
            fi
        done 2>/dev/null <"$nf_dir/status"
        if [ -n "$nf_uid" ] && [ "$nf_uid" != "$uid" ]; then
            echo "$me: note: process ${nf_dir#/proc/} (comm $nf_comm) belongs to uid $nf_uid, not $uid; left running (stop it as that user)"
        fi
    done
}

# CHILD-mode turnserver only: kill the pid named by the relay-owned state
# dir pidfile. A stale pidfile reused by a non-turnserver process is left
# alone (comm check), and a missing pidfile means nothing to do.
reap_child_turnserver() {
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
    [ -f "$turn_pidfile" ] || return 0
    read -r turn_pid 2>/dev/null <"$turn_pidfile" || turn_pid=""
    case "$turn_pid" in
        ''|*[!0-9]*) return 0 ;;
    esac
    kill -0 "$turn_pid" 2>/dev/null || return 0
    [ "$(ps -p "$turn_pid" -o comm= 2>/dev/null | tr -d ' ')" = "turnserver" ] \
        || return 0
    kill "$turn_pid" 2>/dev/null || true
    sleep 1
    if kill -0 "$turn_pid" 2>/dev/null; then
        kill -KILL "$turn_pid" 2>/dev/null || true
    fi
    echo "$me: stopped CHILD turnserver pid $turn_pid ($turn_pidfile)"
}

# --- windows (clean only) ---------------------------------------------

# Browser executables hush-relay launches (hush_exec_native_browser /
# hush_exec_flatpak_browser in hush-c/src/hush_relay.c), by the names
# their processes actually carry, plus the flatpak sandbox (bwrap).
browser_name() {
    bn_raw=$(readlink "/proc/$1/exe" 2>/dev/null) || bn_raw=""
    bn_exe=${bn_raw%" (deleted)"}
    bn_exe=${bn_exe##*/}
    bn_comm=$(proc_comm "$1")
    for bn_n in "$bn_exe" "$bn_comm"; do
        case "$bn_n" in
            brave|brave-browser|bwrap|chromium|chromium-browser|chrome|\
            google-chrome|google-chrome-stable|msedge|microsoft-edge|\
            microsoft-edge-stable|vivaldi|vivaldi-bin)
                printf '%s' "$bn_n"
                return 0
                ;;
        esac
    done
    return 1
}

# Prints the first own-cmdline argument that marks pid $1 as a Hush app
# window: prefix --class=hush-relay, or exactly the --app URL of a relay
# port stopped by this run. Prints nothing otherwise.
window_flag() {
    tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | awk -v ports="$stopped_ports" '
        BEGIN {
            n = split(ports, pa, " ")
            for (i = 1; i <= n; i++)
                want["--app=http://127.0.0.1:" pa[i] "/"] = 1
        }
        NR == 1 { next }
        index($0, "--class=hush-relay") == 1 { print; exit }
        ($0 in want) { print; exit }'
}

# True when pid $1 is a browser main process (no --type= child role).
browser_main() {
    ! tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | grep -q '^--type='
}

window_match() {
    w_name=""
    w_flag=""
    [ "$1" = "$self" ] && return 1
    [ "$1" = "$parent" ] && return 1
    proc_uid "$1"
    [ "$pu_uid" = "$uid" ] || return 1
    alive "$1" || return 1
    w_name=$(browser_name "$1") || return 1
    w_flag=$(window_flag "$1")
    [ -n "$w_flag" ]
}

close_windows() {
    if [ "$have_proc" != 1 ]; then
        echo "$me: no /proc; Hush app windows not scanned (close them by hand)"
        return 0
    fi
    cw_pids=""
    for cw_dir in /proc/[0-9]*; do
        p=${cw_dir#/proc/}
        if window_match "$p"; then
            echo "$me: closing Hush app window pid $p ($w_name) $w_flag"
            cw_pids="$cw_pids $p"
            kill -TERM "$p" 2>/dev/null || true
        elif [ -n "$w_name" ] && [ "$w_name" != "bwrap" ] \
            && alive "$p" && browser_main "$p"; then
            echo "$me: left alone pid $p ($w_name): no --class=hush-relay* or --app of a stopped relay in its own cmdline; a Hush window handed off into this shared browser cannot be closed without closing the whole browser"
        fi
    done
    [ -n "$cw_pids" ] || return 0
    cw_i=0
    while [ "$cw_i" -lt $((grace * 5)) ]; do
        cw_left=0
        for p in $cw_pids; do
            if window_match "$p"; then
                cw_left=1
                break
            fi
        done
        [ "$cw_left" = 0 ] && break
        sleep 0.2
        cw_i=$((cw_i + 1))
    done
    for p in $cw_pids; do
        if window_match "$p"; then
            kill -KILL "$p" 2>/dev/null || true
            sleep 0.2
        fi
        if window_match "$p"; then
            echo "$me: warning: Hush app window pid $p ($w_name) survived SIGKILL" >&2
        else
            echo "$me: closed Hush app window pid $p"
        fi
    done
}

# --- stale renamed copies (clean only) --------------------------------

owned_by_me() {
    [ -n "$(find "$1" -prune -user "$uid" -print 2>/dev/null)" ]
}

is_elf() {
    [ "$(od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n')" = "7f454c46" ]
}

remove_stale_copies() {
    rs_dir=$1
    if [ ! -d "$rs_dir" ]; then
        echo "$me: $rs_dir absent; no stale hush-relay-* copies"
        return 0
    fi
    for f in "$rs_dir"/hush-relay-*; do
        if [ ! -e "$f" ] && [ ! -L "$f" ]; then
            continue # unmatched glob
        fi
        case "${f##*/}" in
            hush-relay-stop)
                echo "$me: kept $f (packaging helper name)"
                continue
                ;;
        esac
        if [ -L "$f" ]; then
            echo "$me: kept $f (symlink; not followed)"
        elif [ ! -f "$f" ]; then
            echo "$me: kept $f (not a regular file)"
        elif ! owned_by_me "$f"; then
            echo "$me: kept $f (not owned by uid $uid)"
        elif ! is_elf "$f"; then
            echo "$me: kept $f (not an ELF executable)"
        elif rm -f -- "$f"; then
            echo "$me: removed stale copy $f"
        else
            echo "$me: FAILED to remove $f" >&2
            rc=1
        fi
    done
}

# --- main -------------------------------------------------------------

mode="${1:-stop}"
bindir=""
case "$mode" in
    stop) ;;
    clean)
        bindir="${2:-}"
        if [ -z "$bindir" ]; then
            echo "$me: usage: kill-relay.sh clean BINDIR (empty BINDIR refused)" >&2
            exit 2
        fi
        ;;
    *)
        echo "$me: usage: kill-relay.sh [stop] | clean BINDIR" >&2
        exit 2
        ;;
esac

stop_relays || rc=1
note_foreign_relays
reap_child_turnserver
if [ "$rc" -ne 0 ]; then
    echo "$me: a hush-relay survived; stop it by hand (Exit in the hive, or kill -KILL the pid above) and retry" >&2
    exit 1
fi
if [ "$mode" = "clean" ]; then
    close_windows
    remove_stale_copies "$bindir"
fi
exit "$rc"
