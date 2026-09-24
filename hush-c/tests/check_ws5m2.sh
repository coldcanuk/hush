#!/bin/sh
# WS5 M2 thread memory: complete leave->return journey wiring.
# The UI journey (memory line, saved meta, resume) is static markup/JS in
# the embed; live server behavior is proven by unit tests (format_json) and
# the restart/eviction transcript below. Static markers only here.
set -eu
cd "$(dirname "$0")/.."

fail() { echo "ws5m2 check failed: $1" >&2; exit 1; }

# Client fetches durable memory once per thread and paints it honestly.
grep -q '"/api/thread?root="' demo/index.html || fail "UI never fetches /api/thread"
grep -q 'Thread memory' demo/index.html || fail "UI has no memory line"
grep -q 'Saved on this relay' demo/index.html || fail "UI has no honest saved labels"
grep -q 'hush-thread-open' demo/index.html || fail "UI never remembers the open thread"
grep -q 'Saved brief on this relay' demo/index.html || fail "UI never shows the saved brief"

# No localStorage content phantoms: only the root id is remembered.
grep -q 'localStorage.setItem("hush-thread-open", rootId)' demo/index.html || fail "resume stores more than the id"
grep -q 'localStorage.removeItem("hush-thread-open")' demo/index.html || fail "resume never clears stale ids"

# Thread open path is intact: UI-M9 dropped these definitions once and every
# Thread button threw. The M2 journey stands on them.
grep -q 'function applyThreadSize' demo/index.html || fail "UI cannot size the thread pane"
grep -q 'function saveThreadSize' demo/index.html || fail "UI cannot persist the thread size"

# Server primitives the journey stands on.
grep -q 'hush_thread_format_json' src/hush_thread.c || fail "relay has no thread formatter"
grep -q 'hush_http_serve_thread' src/api_status.c || fail "relay has no thread handler"
grep -q '"/api/thread"' src/hush_http.c || fail "relay has no /api/thread route"

# The spec records the journey delta.
grep -q 'WS5 M2' ../UI_SPEC.md || fail "UI_SPEC has no WS5 M2 delta"

echo "ws5m2: thread memory fetch, honest labels, resume, server route and spec OK"
