'use strict';
// Preload (NODE_OPTIONS=--require) for tests/check_restart_ui_tmp.sh
// (#326). It only acts inside the fake Chrome's node (the one
// tests/fake-chrome-devtools.sh execs, which has FAKE_CHROME_PORT set);
// in the check itself it does nothing.
//   - It logs "ready" to HUSH_FAKE_CHROME_READY_LOG once the fake's SIGTERM
//     handler is installed. The pin signals only after that line, so a stop
//     can never race the fake's start (a signal sooner kills the fake with
//     no exit line: a start race, not a leak).
//   - It logs "orphan" to HUSH_FAKE_CHROME_EXIT_LOG when the fake exits after
//     the check that started it is gone: the check never stopped it, or did
//     not wait for it to exit (a leak).
const fs = require('node:fs');
/* How often it looks for the fake's SIGTERM handler. */
const POLL_MS = 10;

function watchReady(readyLog) {
  const timer = setInterval(() => {
    if (process.listenerCount('SIGTERM') === 0)
      return;
    clearInterval(timer);
    fs.appendFileSync(readyLog, 'ready\n');
  }, POLL_MS);
  timer.unref();
}
function watchOrphan(exitLog) {
  const parent = process.ppid;
  process.on('exit', () => {
    if (process.ppid !== parent)
      fs.appendFileSync(exitLog, 'orphan\n');
  });
}
if (process.env.FAKE_CHROME_PORT) {
  if (process.env.HUSH_FAKE_CHROME_READY_LOG)
    watchReady(process.env.HUSH_FAKE_CHROME_READY_LOG);
  if (process.env.HUSH_FAKE_CHROME_EXIT_LOG)
    watchOrphan(process.env.HUSH_FAKE_CHROME_EXIT_LOG);
}
