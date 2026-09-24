#!/bin/sh
# check_prefix.sh — an explicit PREFIX is honored, never silently replaced.
#
# Covers the Floor follow-up: `./configure PREFIX=` (and an empty PREFIX
# env) used to fall back to $HOME/.local, so `make clean` / `make install`
# clobbered the shared user prefix the caller tried to avoid. configure
# must now refuse an explicit empty PREFIX, accept VAR=value assignments,
# and both Makefiles must refuse `make ... PREFIX=` (which would otherwise
# resolve BINDIR to /bin) and `make ... BINDIR=` (which would otherwise
# resolve install targets to /hush-relay, root). Packaging/build only: no
# relay behavior changes.
set -eu
cd "$(dirname "$0")/../.."

root=$(pwd)
configure="$root/configure"
topmk="$root/Makefile"
submk="$root/hush-c/Makefile"

fail() { echo "prefix check failed: $1" >&2; exit 1; }

[ -x "$configure" ] || fail "configure missing or not executable ($configure)"
sh -n "$configure" || fail "configure has a syntax error"

# --- static: configure honors VAR=value argv and refuses empty PREFIX ---
grep -q 'PREFIX=\*)' "$configure" \
    || fail "configure ignores PREFIX= argv assignments"
grep -q 'empty PREFIX' "$configure" \
    || fail "configure has no empty-PREFIX refusal"
grep -q -- '--prefix=/tmp/hush-eval' "$configure" \
    || fail "configure hides the isolated-install recipe"

# --- static: both Makefiles guard install/uninstall against empty dirs ---
for mk in "$topmk" "$submk"; do
    grep -q 'check-prefix' "$mk" || fail "$mk has no check-prefix guard"
    grep -q 'empty BINDIR' "$mk" || fail "$mk has no empty-BINDIR refusal"
done
grep -q '^install:.*check-prefix' "$topmk" \
    || fail "top install skips check-prefix"
grep -q '^uninstall:.*check-prefix' "$topmk" \
    || fail "top uninstall skips check-prefix"
grep -q '^clean:.*check-prefix' "$topmk" \
    || fail "top clean skips check-prefix"
grep -q '^install:.*check-prefix' "$submk" \
    || fail "hush-c install skips check-prefix"
grep -q '^uninstall:.*check-prefix' "$submk" \
    || fail "hush-c uninstall skips check-prefix"

# --- behavior: explicit empty PREFIX is refused, never defaulted ---
# configure validates PREFIX before probing or writing config.mk, so these
# run hermetically in a scratch dir and never touch repo state.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$configure" "$tmp/configure"

if (cd "$tmp" && sh ./configure --prefix= >/dev/null 2>&1); then
    fail "./configure --prefix= succeeded; must refuse an empty PREFIX"
fi
if (cd "$tmp" && sh ./configure PREFIX= >/dev/null 2>&1); then
    fail "./configure PREFIX= succeeded; must refuse an empty PREFIX"
fi
if (cd "$tmp" && PREFIX= sh ./configure >/dev/null 2>&1); then
    fail "PREFIX= ./configure succeeded; must refuse an empty PREFIX"
fi
# The refusal must name the problem, not fail opaquely.
(cd "$tmp" && sh ./configure --prefix= 2>&1 || true) | grep -q 'empty PREFIX' \
    || fail "empty-PREFIX refusal never says 'empty PREFIX'"
# And a refused run must not have written a fallback config.
[ ! -f "$tmp/config.mk" ] || fail "refused configure still wrote config.mk"

# --- behavior: an explicit non-empty PREFIX is honored ---
# Probes may fail on machines without dev headers, so only the announced
# directories are asserted, not success.
out=$(cd "$tmp" && sh ./configure --prefix=/tmp/hush-eval-custom 2>&1 || true)
echo "$out" | grep -q 'Target PREFIX:  /tmp/hush-eval-custom' \
    || fail "--prefix= was not honored"
echo "$out" | grep -q 'Binary dir:     /tmp/hush-eval-custom/bin' \
    || fail "BINDIR was not derived from the custom prefix"
out=$(cd "$tmp" && sh ./configure PREFIX=/tmp/hush-eval-argv 2>&1 || true)
echo "$out" | grep -q 'Target PREFIX:  /tmp/hush-eval-argv' \
    || fail "PREFIX= argv assignment was not honored"
out=$(cd "$tmp" && PREFIX=/tmp/hush-eval-env sh ./configure 2>&1 || true)
echo "$out" | grep -q 'Target PREFIX:  /tmp/hush-eval-env' \
    || fail "PREFIX= env assignment was not honored"
rm -f "$tmp/config.mk"

# --- behavior: make refuses an empty PREFIX before touching any prefix ---
if make -C "$root" check-prefix PREFIX= >/dev/null 2>&1; then
    fail "top make check-prefix accepted an empty PREFIX"
fi
make -C "$root" check-prefix "PREFIX=$tmp/iso" >/dev/null 2>&1 \
    || fail "top make check-prefix refused a real PREFIX"
if make -C "$root/hush-c" check-prefix PREFIX= >/dev/null 2>&1; then
    fail "hush-c make check-prefix accepted an empty PREFIX"
fi
make -C "$root/hush-c" check-prefix "PREFIX=$tmp/iso" >/dev/null 2>&1 \
    || fail "hush-c make check-prefix refused a real PREFIX"

# --- behavior: make refuses an empty BINDIR before touching any prefix ---
# An empty BINDIR would install the relay to /hush-relay (root).
if make -C "$root" check-prefix BINDIR= >/dev/null 2>&1; then
    fail "top make check-prefix accepted an empty BINDIR"
fi
make -C "$root" check-prefix "BINDIR=$tmp/iso/bin" >/dev/null 2>&1 \
    || fail "top make check-prefix refused a real BINDIR"
if make -C "$root/hush-c" check-prefix BINDIR= >/dev/null 2>&1; then
    fail "hush-c make check-prefix accepted an empty BINDIR"
fi
make -C "$root/hush-c" check-prefix "BINDIR=$tmp/iso/bin" >/dev/null 2>&1 \
    || fail "hush-c make check-prefix refused a real BINDIR"
# The refusal must name the problem, not fail opaquely.
(make -C "$root" check-prefix BINDIR= 2>&1 || true) | grep -q 'empty BINDIR' \
    || fail "empty-BINDIR refusal never says 'empty BINDIR'"

echo "prefix check ok (explicit PREFIX honored; empty PREFIX refused)"
