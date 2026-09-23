#!/bin/sh
# WS4 stream / cancel / budgets product-journey wiring.
# The UI journey (Stop control, live partials, leash styling) is static
# markup/JS in the embed; live server behavior is proven by
# check_collaboration.py (check_cancel, check_streaming, check_jobcap).
set -eu
cd "$(dirname "$0")/.."

fail() { echo "ws4 check failed: $1" >&2; exit 1; }

# Stop on the thinking chip reaches POST /api/cancel with root + robot.
grep -q '"/api/cancel"' demo/index.html || fail "UI never posts /api/cancel"
grep -q 'think-stop' demo/index.html || fail "UI has no Stop control"
grep -q 'stopJob' demo/index.html || fail "UI has no cancel handler"

# Live partials come from POST /api/reply and paint in place.
grep -q '"/api/reply"' demo/index.html || fail "UI never polls /api/reply"
grep -q 'refreshPartials' demo/index.html || fail "UI never refreshes partials"
grep -q 'data-partial' demo/index.html || fail "UI has no partial preview slot"

# Leash refusals render distinctly from robot answers.
grep -q 'LEASH_MARKS' demo/index.html || fail "UI has no leash markers"
grep -q 'isLeashNote' demo/index.html || fail "UI never marks leash notes"
grep -q 'note.leash' demo/index.html || fail "UI has no leash style"

# Server primitives the journey stands on.
grep -q 'hush_agent_cancel' src/hush_agent.c || fail "relay has no agent cancel"
grep -q 'stopped on request' src/agent_dispatch.c || fail "relay has no stopped note"
grep -q 'hush_agent_partial' src/hush_agent.c || fail "relay has no partial"
grep -q 'HUSH_INTEL_DENY_ROBOT_RATE' src/hush_intel.c || fail "relay has no robot budget"
grep -q '"/api/cancel"' src/hush_http.c || fail "relay has no /api/cancel route"
grep -q '"/api/reply"' src/hush_http.c || fail "relay has no /api/reply route"

# The spec records the journey deltas.
grep -q 'WS4' ../UI_SPEC.md || fail "UI_SPEC has no WS4 delta"

echo "ws4: Stop, partials, leash styling, server primitives and spec OK"
