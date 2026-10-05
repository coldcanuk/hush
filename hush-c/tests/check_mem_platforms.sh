#!/bin/sh
# Prove hush_mem.c builds for NetBSD/Apple sims without explicit_bzero.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"
CFLAGS='-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -c'
SRC="$ROOT/src/hush_mem.c"
INC="-I$ROOT/include"
fail=0

compile() {
    label=$1
    shift
    out="/tmp/hush-mem-sim-$$-$label.o"
    if $CC $CFLAGS "$@" $INC -o "$out" "$SRC" 2>/tmp/hush-mem-sim-err-$$; then
        echo "ok $label"
        rm -f "$out"
    else
        echo "FAIL $label"
        cat /tmp/hush-mem-sim-err-$$
        fail=1
    fi
}

# Host (glibc/etc.): normal include path.
compile host

# NetBSD: must use explicit_memset, not explicit_bzero.
compile netbsd -D__NetBSD__ -U__GLIBC__ -U__FreeBSD__ -U__OpenBSD__ -U__APPLE__ \
    -I"$ROOT/tests/platform-sim/netbsd"

# Apple: must use memset_s.
compile apple -D__APPLE__ -U__GLIBC__ -U__FreeBSD__ -U__OpenBSD__ -U__NetBSD__ \
    -I"$ROOT/tests/platform-sim/apple"

# No named secure API: volatile memset fallback.
compile fallback -U__GLIBC__ -U__FreeBSD__ -U__OpenBSD__ -U__NetBSD__ -U__APPLE__ \
    -I"$ROOT/tests/platform-sim/fallback"

rm -f /tmp/hush-mem-sim-err-$$
if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check_mem_platforms ok"
