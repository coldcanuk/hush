#!/bin/sh
# WS5 M2 thread memory after M1 (channels only, PLAN_ROUTER_WORKER_V2 r1
# §5.2-§5.3): thread memory is internal transport. The browser never fetches,
# paints, or resumes it; the relay keeps the store and the /api/thread route
# until M4+ (proven by unit tests and the restart transcript). Static markers.
set -eu
cd "$(dirname "$0")/.."

fail() { echo "ws5m2 check failed: $1" >&2; exit 1; }

# The UI carries no thread memory journey (Ops CH0_HARD).
for gone in '/api/thread' 'Thread memory' 'Saved on this relay' 'hush-thread-open' \
    'function pinThreadNewest' 'function applyThreadSize' 'function saveThreadSize' \
    '· Decision'; do
  if grep -qF "$gone" demo/index.html; then fail "thread memory UI must be gone (M1): $gone"; fi
done

# Owner-only Yes/No stamps answer loop, approval and team asks in the channel.
grep -q 'function isHiveOwner' demo/index.html || fail "loop answers must be owner only"
grep -q 'className = "loop-stamp loop-yes"' demo/index.html || fail "missing Yes stamp"
grep -q 'className = "loop-stamp loop-no"' demo/index.html || fail "missing No stamp"
grep -q 'if (isHiveOwner() && isYesNoAsk(e.content))' demo/index.html \
    || fail "Yes/No stamps must be owner only"

# Server primitives stay as internal transport (M4+ removes the route).
grep -q 'hush_thread_format_json' src/hush_thread.c || fail "relay has no thread formatter"
grep -q 'hush_http_serve_thread' src/api_status.c || fail "relay has no thread handler"
grep -q '"/api/thread"' src/hush_http.c || fail "relay has no /api/thread route"

# The spec records the journey delta.
grep -q 'WS5 M2' ../UI_SPEC.md || fail "UI_SPEC has no WS5 M2 delta"

echo "ws5m2: no thread memory UI, owner Yes/No stamps, internal server route and spec OK"
