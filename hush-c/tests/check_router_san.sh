#!/bin/sh
# Pin for #375 Gauge F1: test_router must run clean under ASan + UBSan.
# Router and JSON sources are rebuilt sanitized; the rest links from libhush.a.
set -eu
cd "$(dirname "$0")/.."
fail() { echo "router sanitizer check failed: $1" >&2; exit 1; }
cc=${CC:-cc}
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
flags="-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O1 -g"
[ -f libhush.a ] || fail "libhush.a is missing; run make first"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
# Pin for #375 Gauge F2: router sources stay ISO C11 (no GNU enum widening).
for src in src/hush_router.c src/api_router.c tests/test_router.c; do
    # shellcheck disable=SC2086
    $cc $flags -Wpedantic -pedantic-errors $(pkg-config --cflags libcurl) \
        -fsyntax-only "$src" || fail "$src is not ISO C11 (-pedantic-errors)"
done
printf 'int main(void){return 0;}\n' >"$out/probe.c"
if ! $cc $san "$out/probe.c" -o "$out/probe" 2>/dev/null; then
    # Only Linux CI is required to carry the sanitizer runtimes.
    [ "$(uname -s)" = Linux ] && fail "$cc cannot link $san"
    echo "router sanitizer check: SKIP ($cc has no sanitizer runtime)"
    exit 0
fi
# shellcheck disable=SC2086
$cc $flags $san -fno-omit-frame-pointer tests/test_router.c src/hush_router.c \
    src/hush_json.c src/hush_json_read.c libhush.a -o "$out/test_router" \
    -lcrypto $(pkg-config --libs libcurl) || fail "sanitized build"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$out/test_router" || fail "test_router under ASan/UBSan"
echo "router sanitizer check: OK"
