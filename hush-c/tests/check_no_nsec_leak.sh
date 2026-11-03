#!/bin/sh
# check_no_nsec_leak.sh — the last step of `make test`: no Hush private key
# file (*nsec*) made during the run may sit outside the run's TMPDIR, and
# none may be left inside it.
#
#   check_no_nsec_leak.sh mark <marker>
#       Creates <marker> with an mtime one second before now. The second
#       of slack covers coarse filesystem timestamps, so a file made in the
#       same tick as the marker still reads as newer.
#   check_no_nsec_leak.sh check <marker> <run-tmpdir>
#       Fails (exit 1, offending paths printed) when
#        (a) /tmp holds a non-directory named *nsec*, owned by this uid and
#            newer than <marker>, outside <run-tmpdir>; or
#        (b) anything named *nsec* is left under <run-tmpdir>.
#       Then lists, as INFO only, the other entries left at the top of
#       <run-tmpdir> (the marker and the hush-make-* dirs `make test` owns
#       are skipped).
#
# Only metadata is printed (mode, owner, path); no file is ever opened.
# Scope: -user and -newer keep other users' files and every file that
# existed before the run out of (a), so a shared /tmp does not make it
# flaky. Remaining limit: another run by the SAME uid that writes *nsec*
# into /tmp while this run is going (a second `make test`, another lane on
# the same account) is reported as this run's leak. Run one suite per uid
# at a time, or read the printed paths before acting on a failure.
set -eu

SCAN_ROOT=/tmp
MARK_SLACK_S=1

fail() { echo "nsec leak check failed: $1" >&2; exit 1; }

# Prints "<mode> <owner> <path>" for each match find writes on stdin.
describe() {
    while IFS= read -r path; do
        printf '  %s\n' "$(stat -c '%a %U' -- "$path" 2>/dev/null || echo '? ?') $path"
    done
}

mark() {
    marker=$1
    now=$(date +%s)
    : > "$marker"
    touch -d "@$((now - MARK_SLACK_S))" "$marker"
}

# (a) *nsec* newer than the marker, owned by this uid, outside the run dir.
# find's own errors (other users' unreadable dirs) are dropped on purpose.
leaks_outside() {
    marker=$1
    run=$2
    find "$SCAN_ROOT" -xdev -path "$run" -prune -o \
        -user "$(id -u)" -newer "$marker" -name '*nsec*' ! -type d -print \
        2>/dev/null || true
}

# (b) anything named *nsec* still under the run dir.
leaks_inside() {
    run=$1
    find "$run" -name '*nsec*' -print 2>/dev/null || true
}

# INFO: top-level leftovers under the run dir, besides make's own dirs.
leftovers() {
    marker=$1
    run=$2
    find "$run" -mindepth 1 -maxdepth 1 ! -name "$(basename "$marker")" \
        ! -name 'hush-make-*' -print 2>/dev/null || true
}

check() {
    marker=$1
    [ -f "$marker" ] || fail "no marker at $marker"
    [ -d "$2" ] || fail "no run TMPDIR at $2"
    run=$(cd "$2" && pwd -P)
    [ "$run" != "$(cd "$SCAN_ROOT" && pwd -P)" ] || fail "run TMPDIR must be a private dir, not $SCAN_ROOT"
    outside=$(leaks_outside "$marker" "$run")
    inside=$(leaks_inside "$run")
    rest=$(leftovers "$marker" "$run")
    status=0
    if [ -n "$outside" ]; then
        echo "nsec leak: $(printf '%s\n' "$outside" | wc -l) file(s) outside $run:" >&2
        printf '%s\n' "$outside" | describe >&2
        status=1
    fi
    if [ -n "$inside" ]; then
        echo "nsec leak: $(printf '%s\n' "$inside" | wc -l) entr(ies) left under $run:" >&2
        printf '%s\n' "$inside" | describe >&2
        status=1
    fi
    if [ -n "$rest" ]; then
        echo "INFO: $(printf '%s\n' "$rest" | wc -l) other top-level entr(ies) left under $run (not keys; not a failure)"
    fi
    [ "$status" -eq 0 ] || exit 1
    echo "nsec leak check ok: none outside $run, none left in it"
}

[ $# -ge 2 ] || fail "usage: $0 mark <marker> | check <marker> <run-tmpdir>"
case "$1" in
    mark)
        mark "$2"
        ;;
    check)
        [ $# -eq 3 ] || fail "usage: $0 check <marker> <run-tmpdir>"
        check "$2" "$3"
        ;;
    *)
        fail "unknown mode $1"
        ;;
esac
