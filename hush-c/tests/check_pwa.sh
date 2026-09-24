#!/bin/sh
# Smoke-test PWA routes on a throwaway hush-relay.
set -eu
cd "$(dirname "$0")/.."
# Session-token gate plus a hermetic pass store, so the harness never reads the
# operator's real credentials. curl() adds the hive token to every call.
test_home="$(mktemp -d)"
export HUSH_HOME="${HUSH_HOME:-$test_home/hush}"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$(mktemp -d)"
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }

bin=./hush-relay
port=18765
log=$(mktemp)
cfg=$(mktemp -d)
export HUSH_CONFIG_DIR="$cfg"
"$bin" --no-open "$port" >"$log" 2>&1 &
pid=$!
cleanup() { kill "$pid" 2>/dev/null || true; rm -f "$log"; rm -rf "$cfg"; }
trap cleanup EXIT
i=0
while [ "$i" -lt 50 ]; do
    if curl -sf "http://127.0.0.1:${port}/api/status" >/dev/null 2>&1; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done
fail() { echo "PWA check failed: $1" >&2; exit 1; }
html=$(curl -sf "http://127.0.0.1:${port}/")
echo "$html" | grep -q 'rel="manifest"' || fail "HTML missing manifest link"
echo "$html" | grep -q 'serviceWorker' || fail "HTML missing SW register"
echo "$html" | grep -q 'id="provider-oauth"' || fail "HTML missing OAuth button"
echo "$html" | grep -q 'Log in with Grok OAuth' || fail "HTML missing Grok OAuth copy"
echo "$html" | grep -q 'grok login --oauth' || fail "HTML missing grok login --oauth"
echo "$html" | grep -q 'a browser should open' || fail "HTML missing OAuth hold copy"
echo "$html" | grep -q 'id="provider-key-add"' || fail "HTML missing provider + button"
echo "$html" | grep -q 'OAUTH_PROVIDERS' || fail "HTML missing OAuth map"
echo "$html" | grep -q 'id="msg"' || fail "HTML missing composer"
echo "$html" | grep -q 'rows="6"' || fail "HTML missing 6-line composer"
echo "$html" | grep -q 'THREAD_PIN_PX' || fail "HTML missing thread pin"
echo "$html" | grep -q 'id="code-canvas-hi"' || fail "HTML missing canvas highlight"
echo "$html" | grep -q 'id="canvas-k"' || fail "HTML missing canvas Ctrl+K"
echo "$html" | grep -q 'golang' || fail "HTML missing golang alias"
# UI-M9: persistent tool rail deleted; header KIT menu only. The rail-i
# micro-UX info buttons are gone with the strip; action hooks stay.
if echo "$html" | grep -q 'id="tool-rail"'; then fail "tool rail must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-grip'; then fail "rail grip must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'rail-body'; then fail "rail body must be gone (UI-M9)"; fi
if echo "$html" | grep -q 'id="rail-info"'; then fail "rail info must be gone (UI-M9)"; fi
echo "$html" | grep -q 'id="kit-menu"' || fail "HTML missing KIT menu overlay (UI-M9)"
echo "$html" | grep -q 'id="providers-btn"' || fail "HTML missing configure providers"
# UI-M11: volume dial owns dispatch-log scroll (supersedes UI-M10).
# Native fat bar hidden; message column owns NO in-column chrome.
if echo "$html" | grep -q 'id="fo-scrollbar"'; then fail "M10 scrollbar must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-track"'; then fail "M10 scroll track must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-dot"'; then fail "M10 scroll dot must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-up"'; then fail "M10 scroll up arrow must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'id="fo-scroll-down"'; then fail "M10 scroll down arrow must be gone (UI-M11)"; fi
if echo "$html" | grep -q 'syncFoScrollbar'; then fail "M10 scrollbar sync must be gone (UI-M11)"; fi
echo "$html" | grep -q 'id="fo-dial"' || fail "HTML missing volume dial (UI-M11)"
echo "$html" | grep -q 'id="fo-dial-knob"' || fail "HTML missing dial knob (UI-M11)"
echo "$html" | grep -q 'syncFoDial' || fail "HTML missing dial sync (UI-M11)"
echo "$html" | grep -q 'UI-M11' || fail "HTML missing UI-M11 markers"
echo "$html" | grep -q 'id="providers-hub"' || fail "HTML missing providers hub"
echo "$html" | grep -q 'openProviderDrawer' || fail "HTML missing openProviderDrawer"
echo "$html" | grep -q 'Raise a new robot' || fail "HTML missing robot copy"
echo "$html" | grep -q 'id="invite-human"' || fail "HTML missing invite"
echo "$html" | grep -q 'id="add-chan"' || fail "HTML missing add channel"
echo "$html" | grep -q 'id="raise-agent"' || fail "HTML missing new robot"
echo "$html" | grep -q 'id="add-proj"' || fail "HTML missing new project"
echo "$html" | grep -q 'id="rail-min"' || fail "HTML missing minimize"
echo "$html" | grep -q 'id="rail-max"' || fail "HTML missing maximize"
echo "$html" | grep -q '/api/fixup' || fail "HTML missing fixup POST"
echo "$html" | grep -q '/api/complete' || fail "HTML missing complete POST"
echo "$html" | grep -q 'tok-ghost' || fail "HTML missing ghost class"
echo "$html" | grep -q 'fim-caret' || fail "HTML missing fim caret"
echo "$html" | grep -q 'activePrediction' || fail "HTML missing activePrediction"
echo "$html" | grep -q 'CANVAS_FIM_MS' || fail "HTML missing FIM debounce"
echo "$html" | grep -q '/api/window' || fail "HTML missing window POST"
echo "$html" | grep -q 'class="create"' && fail "HTML still has left-nav Create"
grep -q -- '--ozone-platform=x11' src/hush_relay.c || fail "launch missing ozone-x11"
mf=$(curl -sf "http://127.0.0.1:${port}/manifest.webmanifest")
echo "$mf" | grep -q '"short_name"' || fail "manifest short_name"
echo "$mf" | grep -q '192x192' || fail "manifest 192"
echo "$mf" | grep -q '512x512' || fail "manifest 512"
echo "$mf" | grep -q '"standalone"' || fail "manifest display"
curl -sf "http://127.0.0.1:${port}/sw.js" | grep -q 'HUSH_CACHE' || fail "sw.js"
png192=$(curl -sf "http://127.0.0.1:${port}/icon-192.png" | od -An -tx1 | head -1)
echo "$png192" | grep -q '89 50 4e 47' || fail "icon-192 not PNG"
png512=$(curl -sf "http://127.0.0.1:${port}/icon-512.png" | od -An -tx1 | head -1)
echo "$png512" | grep -q '89 50 4e 47' || fail "icon-512 not PNG"
curl -sf "http://127.0.0.1:${port}/apple-touch-icon.png" >/dev/null || fail "apple-touch-icon"
echo "PWA routes ok"
