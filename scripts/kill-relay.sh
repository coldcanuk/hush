#!/bin/sh
# kill-relay.sh — stop every running hush-relay owned by this user before
# `make install` / `make clean`, renamed copies included (#221).
#
# Usage:
#   sh scripts/kill-relay.sh [stop]        # make install (via stop-relays)
#   sh scripts/kill-relay.sh clean BINDIR  # make clean (via clean-relays)
#
# Whose relays: processes whose real uid is the caller's. Under sudo (uid 0
# with SUDO_UID set, e.g. `sudo make install PREFIX=/usr`) also SUDO_UID's,
# so the invoking user's relay does not keep serving the replaced binary.
# Any other uid's relay is never signalled; it only gets a "note:" line.
#
# stop (default)
#   On Linux a candidate is a live process of an allowed uid whose
#   executable basename (readlink /proc/PID/exe) starts with "hush-relay":
#   hush-relay, hush-relay-m11-e2596637 and a replaced binary (the kernel
#   appends " (deleted)" after the basename, which a prefix test ignores)
#   all match. Never matched by exact name, by port, or by cmdline text.
#   A hush-relay* comm whose exe link is unreadable is NOT signalled: it is
#   reported as left running and counted as a survivor (exit 1). An exe
#   path containing a newline is skipped with a note, also a survivor.
#   This script and every ancestor (the recipe shell, make, ... up to pid
#   1) are never candidates. If any ancestor IS a hush-relay (e.g. an agent
#   the relay spawned runs `make install`), the script refuses: it prints
#   that pid and path, signals nothing and exits 1.
#   Each candidate gets SIGTERM, then SIGKILL after HUSH_KILL_GRACE_S
#   seconds (default 3). uid and exe path are re-checked immediately
#   before each signal; this is not atomic (no pidfd). A pid that turns
#   into a different hush-relay* between checks is reported "changed
#   identity, not signalled" and counted as a survivor. Every pid and path
#   stopped is printed. Also reaps the relay's CHILD-mode turnserver.
#   Exit 0 when nothing ran or everything stopped; exit 1 when a relay
#   survived or could not be verified, an ancestor is a relay, or /proc
#   is unreadable on Linux.
#
# clean
#   stop, then close Hush app windows, then remove stale renamed
#   hush-relay-* copies from BINDIR. Never touches ~/.hush or HUSH_HOME.
#   Windows: only Chromium-family / bwrap processes (the browsers
#   hush-relay launches) of an allowed uid whose OWN cmdline has an
#   argument starting with --class=hush-relay (hush-relay always passes
#   one: HUSH_UI_CLASS_OPTION in hush-c/src/hush_relay.c). An
#   --app=http://127.0.0.1:<port of a relay just stopped>/ argument is
#   only reported alongside; a browser with that --app but no
#   --class=hush-relay is left running with a note. A Hush window that a
#   browser handed off into an already-running shared browser process
#   lives in a process without those flags; closing it would kill the
#   user's whole browser, so such processes are only reported as "left
#   alone", never signalled.
#   Files: only regular files (never symlinks, never followed) named
#   hush-relay-* directly in BINDIR, owned by an allowed uid, starting
#   with the ELF magic. hush-relay-stop (the packaging helper, shipped to
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
# Not Linux (*BSD): there is no /proc exe identity, so relays are matched
# by `ps` comm (prefix hush-relay, allowed uids only; the pre-#221 script
# also matched by ps comm), ancestors come from `ps -o ppid=`, and windows
# are not closed. Untested in CI. On Linux an unreadable /proc is an
# error, never a fallback.

set -u

me="kill-relay"
say() { printf '%s: %s\n' "$me" "$*"; }
warn() { printf '%s: %s\n' "$me" "$*" >&2; }
# Fractional sleep where supported; never a zero-length grace period.
nap() { sleep 0.2 2>/dev/null || sleep 1; }

nl='
'
uid=$(id -u)
self=$$
grace="${HUSH_KILL_GRACE_S:-3}"
case "$grace" in
    ''|*[!0-9]*) grace=3 ;;
esac
if [ "$grace" -lt 1 ]; then
    grace=1
fi

allowed_uids="$uid"
if [ "$uid" = 0 ]; then
    case "${SUDO_UID:-}" in
        ''|*[!0-9]*|0) ;;
        *)
            allowed_uids="0 $SUDO_UID"
            say "running as root via sudo: stopping hush-relay* of uid 0 and SUDO_UID $SUDO_UID only"
            ;;
    esac
fi

uid_allowed() {
    case " $allowed_uids " in
        *" $1 "*) return 0 ;;
    esac
    return 1
}

have_proc=0
if [ "$(uname -s 2>/dev/null)" = "Linux" ]; then
    if [ -r "/proc/$self/status" ] && readlink "/proc/$self/exe" >/dev/null 2>&1; then
        have_proc=1
    else
        warn "error: /proc is not readable, so relay identities cannot be verified; nothing signalled"
        exit 1
    fi
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

# Sets pp_pid to the parent pid of $1, empty when unknown.
ppid_of() {
    pp_pid=""
    if [ "$have_proc" = 1 ]; then
        while read -r pp_key pp_val _; do
            if [ "$pp_key" = "PPid:" ]; then
                pp_pid=$pp_val
                break
            fi
        done 2>/dev/null <"/proc/$1/status"
    else
        pp_pid=$(ps -o ppid= -p "$1" 2>/dev/null | tr -d ' ')
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

# Kernel command name of pid $1.
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

# Every pid on the system, one per line.
all_pids() {
    if [ "$have_proc" = 1 ]; then
        for ap_dir in /proc/[0-9]*; do
            printf '%s\n' "${ap_dir#/proc/}"
        done
    else
        ps -ax -o pid= 2>/dev/null | tr -d ' '
    fi
}

# --- ancestors ----------------------------------------------------------

# This script's ancestors, from the PPid chain up to (not including) pid 1:
# the recipe shell, make, whatever ran make. Never candidates.
ancestors=""
an_pid=$self
an_n=0
while [ "$an_n" -lt 1024 ]; do
    ppid_of "$an_pid"
    case "$pp_pid" in
        ''|*[!0-9]*) break ;;
    esac
    [ "$pp_pid" -gt 1 ] || break
    ancestors="$ancestors $pp_pid"
    an_pid=$pp_pid
    an_n=$((an_n + 1))
done

is_ancestor() {
    case " $ancestors " in
        *" $1 "*) return 0 ;;
    esac
    return 1
}

# Refuses (exit 1, nothing signalled) when any ancestor is a hush-relay,
# of any uid: stopping it would kill the process running this make.
# Refusing is the safe direction, so an unreadable exe falls back to comm
# here (and only here).
refuse_if_ancestor_relay() {
    for ra_pid in $ancestors; do
        ra_path=""
        ra_hit=0
        if [ "$have_proc" = 1 ]; then
            ra_path=$(readlink "/proc/$ra_pid/exe" 2>/dev/null) || ra_path=""
        fi
        if [ -n "$ra_path" ]; then
            case "${ra_path##*/}" in
                hush-relay*) ra_hit=1 ;;
            esac
        else
            ra_comm=$(proc_comm "$ra_pid")
            case "$ra_comm" in
                hush-relay*)
                    ra_hit=1
                    ra_path="(exe unreadable; comm $ra_comm)"
                    ;;
            esac
        fi
        if [ "$ra_hit" = 1 ]; then
            warn "refusing: ancestor pid $ra_pid exe $ra_path is a hush-relay (this command runs under it); nothing signalled. Stop that relay from outside its process tree, then retry."
            exit 1
        fi
    done
}

# Classifies pid $1 and sets m_path / m_how:
#   0  verified hush-relay* of an allowed uid (Linux: by /proc exe)
#   1  not a candidate
#   2  hush-relay* comm but exe link unreadable: never signalled
#   3  hush-relay* exe whose path contains a newline: never signalled
relay_match() {
    m_path=""
    m_how=""
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ "$1" -gt 1 ] || return 1
    [ "$1" = "$self" ] && return 1
    is_ancestor "$1" && return 1
    proc_uid "$1"
    [ -n "$pu_uid" ] || return 1
    uid_allowed "$pu_uid" || return 1
    alive "$1" || return 1
    if [ "$have_proc" = 1 ]; then
        rm_raw=$(readlink "/proc/$1/exe" 2>/dev/null) || rm_raw=""
        if [ -n "$rm_raw" ]; then
            # A " (deleted)" suffix follows the basename, so this prefix
            # test matches replaced binaries without stripping anything.
            case "${rm_raw##*/}" in
                hush-relay*) ;;
                *) return 1 ;;
            esac
            m_path=$rm_raw
            m_how="exe"
            case "$rm_raw" in
                *"$nl"*) return 3 ;;
            esac
            return 0
        fi
        rm_comm=$(proc_comm "$1")
        case "$rm_comm" in
            hush-relay*)
                m_path="(exe unreadable; comm $rm_comm)"
                m_how="comm"
                return 2
                ;;
        esac
        return 1
    fi
    # Not Linux: ps comm is the only identity available.
    rm_comm=$(proc_comm "$1")
    case "$rm_comm" in
        hush-relay*)
            m_path="(comm $rm_comm)"
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

# --- stop -------------------------------------------------------------

stop_relays() {
    sr_status=0
    sr_table=""
    for p in $(all_pids); do
        relay_match "$p"
        case $? in
            0)
                port=$(relay_port "$p")
                say "found hush-relay pid $p exe $m_path port ${port:-unknown} (match: $m_how)"
                if [ -n "$port" ]; then
                    stopped_ports="$stopped_ports $port"
                fi
                sr_table="$sr_table$p $m_path$nl"
                kill -TERM "$p" 2>/dev/null || true
                ;;
            2)
                say "left running pid $p $m_path: exe link unreadable, identity unverified; not signalled"
                sr_status=1
                ;;
            3)
                say "left running pid $p: hush-relay* exe path contains a newline; not signalled"
                sr_status=1
                ;;
        esac
    done
    if [ -z "$sr_table" ]; then
        if [ "$sr_status" = 0 ]; then
            say "no running hush-relay* process for uid $allowed_uids"
        fi
        return "$sr_status"
    fi

    sr_tries=$((grace * 5))
    sr_i=0
    while [ "$sr_i" -lt "$sr_tries" ]; do
        sr_left=0
        sr_rest=$sr_table
        while [ -n "$sr_rest" ]; do
            sr_line=${sr_rest%%"$nl"*}
            sr_rest=${sr_rest#*"$nl"}
            relay_match "${sr_line%% *}"
            case $? in
                0|2) sr_left=1 ;;
            esac
        done
        [ "$sr_left" = 0 ] && break
        nap
        sr_i=$((sr_i + 1))
    done

    sr_rest=$sr_table
    while [ -n "$sr_rest" ]; do
        sr_line=${sr_rest%%"$nl"*}
        sr_rest=${sr_rest#*"$nl"}
        p=${sr_line%% *}
        path=${sr_line#* }
        [ -n "$p" ] || continue
        relay_match "$p"
        sr_m=$?
        if [ "$sr_m" = 1 ]; then
            say "stopped pid $p $path (SIGTERM)"
            continue
        fi
        if [ "$sr_m" != 0 ] || [ "$m_path" != "$path" ]; then
            say "pid $p changed identity (was $path, now $m_path), not signalled"
            sr_status=1
            continue
        fi
        kill -KILL "$p" 2>/dev/null || true
        nap
        relay_match "$p"
        sr_m=$?
        if [ "$sr_m" = 1 ]; then
            say "stopped pid $p $path (SIGKILL after ${grace}s)"
        else
            warn "FAILED to stop pid $p $path (still present after SIGKILL)"
            sr_status=1
        fi
    done
    return "$sr_status"
}

# Other users' relays are never signalled; name them so an operator knows
# a relay still serves the old binary. comm is world-readable; nothing
# here sends a signal.
note_foreign_relays() {
    [ "$have_proc" = 1 ] || return 0
    for nf_dir in /proc/[0-9]*; do
        nf_comm=""
        read -r nf_comm 2>/dev/null <"$nf_dir/comm" || continue
        case "$nf_comm" in
            hush-relay*) ;;
            *) continue ;;
        esac
        proc_uid "${nf_dir#/proc/}"
        if [ -n "$pu_uid" ] && ! uid_allowed "$pu_uid"; then
            say "note: process ${nf_dir#/proc/} (comm $nf_comm) belongs to uid $pu_uid, not $allowed_uids; left running (stop it as that user)"
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
    say "stopped CHILD turnserver pid $turn_pid ($turn_pidfile)"
}

# --- windows (clean only) ---------------------------------------------

# Browser executables hush-relay launches (hush_exec_native_browser /
# hush_exec_flatpak_browser in hush-c/src/hush_relay.c), by the names
# their processes actually carry, plus the flatpak sandbox (bwrap).
# Here the " (deleted)" strip matters: the names are compared exactly.
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

# First own-cmdline argument of pid $1 starting with --class=hush-relay.
window_class() {
    tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | awk '
        NR == 1 { next }
        index($0, "--class=hush-relay") == 1 { print; exit }'
}

# Own-cmdline argument of pid $1 that is exactly the --app URL of a relay
# port stopped by this run (reporting only; never enough to close).
window_app() {
    tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | awk -v ports="$stopped_ports" '
        BEGIN {
            n = split(ports, pa, " ")
            for (i = 1; i <= n; i++)
                want["--app=http://127.0.0.1:" pa[i] "/"] = 1
        }
        NR == 1 { next }
        ($0 in want) { print; exit }'
}

# True when pid $1 is a browser main process (no --type= child role).
browser_main() {
    ! tr '\0' '\n' 2>/dev/null <"/proc/$1/cmdline" | grep -q '^--type='
}

# Sets w_name / w_class / w_app for pid $1. True only for a live browser
# of an allowed uid (never this script or an ancestor) whose own cmdline
# carries --class=hush-relay*.
window_match() {
    w_name=""
    w_class=""
    w_app=""
    [ "$1" = "$self" ] && return 1
    is_ancestor "$1" && return 1
    proc_uid "$1"
    [ -n "$pu_uid" ] || return 1
    uid_allowed "$pu_uid" || return 1
    alive "$1" || return 1
    w_name=$(browser_name "$1") || w_name=""
    [ -n "$w_name" ] || return 1
    w_class=$(window_class "$1")
    w_app=$(window_app "$1")
    [ -n "$w_class" ]
}

close_windows() {
    if [ "$have_proc" != 1 ]; then
        say "no /proc; Hush app windows not scanned (close them by hand)"
        return 0
    fi
    cw_pids=""
    for p in $(all_pids); do
        if window_match "$p"; then
            say "closing Hush app window pid $p ($w_name) $w_class${w_app:+ $w_app}"
            cw_pids="$cw_pids $p"
            kill -TERM "$p" 2>/dev/null || true
        elif [ -n "$w_name" ] && [ -n "$w_app" ]; then
            say "left running pid $p ($w_name): $w_app of a stopped relay but no --class=hush-relay in its own cmdline; not signalled"
        elif [ -n "$w_name" ] && [ "$w_name" != "bwrap" ] && browser_main "$p"; then
            say "left alone pid $p ($w_name): no --class=hush-relay* in its own cmdline; a Hush window handed off into this shared browser cannot be closed without closing the whole browser"
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
        nap
        cw_i=$((cw_i + 1))
    done
    for p in $cw_pids; do
        if window_match "$p"; then
            kill -KILL "$p" 2>/dev/null || true
            nap
        fi
        if window_match "$p"; then
            warn "warning: Hush app window pid $p ($w_name) survived SIGKILL"
        else
            say "closed Hush app window pid $p"
        fi
    done
}

# --- stale renamed copies (clean only) --------------------------------

owned_by_allowed() {
    for ob_uid in $allowed_uids; do
        if [ -n "$(find "$1" -prune -user "$ob_uid" -print 2>/dev/null)" ]; then
            return 0
        fi
    done
    return 1
}

is_elf() {
    [ "$(od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n')" = "7f454c46" ]
}

remove_stale_copies() {
    rs_dir=$1
    if [ ! -d "$rs_dir" ]; then
        say "$rs_dir absent; no stale hush-relay-* copies"
        return 0
    fi
    for f in "$rs_dir"/hush-relay-*; do
        if [ ! -e "$f" ] && [ ! -L "$f" ]; then
            continue # unmatched glob
        fi
        case "$f" in
            *"$nl"*)
                say "kept a hush-relay-* file whose name contains a newline"
                continue
                ;;
        esac
        case "${f##*/}" in
            hush-relay-stop)
                say "kept $f (packaging helper name)"
                continue
                ;;
        esac
        if [ -L "$f" ]; then
            say "kept $f (symlink; not followed)"
        elif [ ! -f "$f" ]; then
            say "kept $f (not a regular file)"
        elif ! owned_by_allowed "$f"; then
            say "kept $f (not owned by uid $allowed_uids)"
        elif ! is_elf "$f"; then
            say "kept $f (not an ELF executable)"
        elif rm -f -- "$f"; then
            say "removed stale copy $f"
        else
            warn "FAILED to remove $f"
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
            warn "usage: kill-relay.sh clean BINDIR (empty BINDIR refused)"
            exit 2
        fi
        ;;
    *)
        warn "usage: kill-relay.sh [stop] | clean BINDIR"
        exit 2
        ;;
esac

refuse_if_ancestor_relay
stop_relays || rc=1
note_foreign_relays
reap_child_turnserver
if [ "$rc" -ne 0 ]; then
    warn "a hush-relay survived or could not be verified; stop it by hand (Exit in the hive, or kill it by pid) and retry"
    exit 1
fi
if [ "$mode" = "clean" ]; then
    close_windows
    remove_stale_copies "$bindir"
fi
exit "$rc"
