#!/bin/sh
# Test double for #265: pretends to be Chrome. Never prints
# "DevTools listening on …" to stderr (the race the old wait scraped).
# Honours --remote-debugging-port=N and serves /json/list after an optional
# delay (HUSH_FAKE_CHROME_DELAY_MS, default 100).
set -eu
port=""
user_dir=""
for arg in "$@"; do
    case "$arg" in
        --remote-debugging-port=*)
            port="${arg#--remote-debugging-port=}"
            ;;
        --user-data-dir=*)
            user_dir="${arg#--user-data-dir=}"
            ;;
    esac
done
if [ -z "$port" ] || [ "$port" = "0" ]; then
    echo "fake-chrome: need a fixed --remote-debugging-port" >&2
    exit 2
fi
delay_ms="${HUSH_FAKE_CHROME_DELAY_MS:-100}"
# Optional dbus-like noise on stderr (matches flake logs; must not unlock wait).
echo "Fake chrome dbus noise: Failed to connect to the bus" >&2

# Tiny HTTP server for /json/list and /json/version via node (no deps).
export FAKE_CHROME_PORT="$port"
export FAKE_CHROME_DELAY_MS="$delay_ms"
export FAKE_CHROME_USER_DIR="$user_dir"
exec node <<'NODE'
'use strict';
const http = require('node:http');
const port = Number(process.env.FAKE_CHROME_PORT);
const delay = Number(process.env.FAKE_CHROME_DELAY_MS || 100);
const pageWs = `ws://127.0.0.1:${port}/devtools/page/fake`;
const browserWs = `ws://127.0.0.1:${port}/devtools/browser/fake`;
// HUSH_FAKE_CHROME_NO_PAGE=1 lists no page target (a harness failure path).
const list = JSON.stringify(process.env.HUSH_FAKE_CHROME_NO_PAGE === '1' ? [] : [{
  type: 'page',
  webSocketDebuggerUrl: pageWs,
  url: 'about:blank',
}]);
const version = JSON.stringify({
  Browser: 'fake-chrome',
  webSocketDebuggerUrl: browserWs,
});
setTimeout(() => {
  http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json');
    if (req.url && req.url.startsWith('/json/version'))
      res.end(version);
    else
      res.end(list);
  }).listen(port, '127.0.0.1');
}, delay);
// Like real Chrome, (re)create the profile and write into it while shutting
// down on SIGTERM, so a harness that removes the profile before its Chrome
// has exited leaves a dir behind (tests/check_restart_ui_tmp.sh).
process.on('SIGTERM', () => {
  const dir = process.env.FAKE_CHROME_USER_DIR || '';
  if (dir) {
    const fs = require('node:fs');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(require('node:path').join(dir, 'Last Session'), 'x');
  }
  process.exit(0);
});
// Stay alive until killed.
setInterval(() => {}, 60000);
NODE
