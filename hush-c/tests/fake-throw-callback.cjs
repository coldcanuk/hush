'use strict';
// Preload (node --require) for tests/check_restart_ui_tmp.sh (#326). Once
// the check has made its Chrome profile and the fake Chrome is ready, it throws from a timer callback,
// outside every promise the check awaits, so only the check's
// uncaughtException handler can clean up. HUSH_FAKE_THROW=reject makes it
// leave a rejected promise instead (the unhandledRejection handler).
const fs = require('node:fs');
const os = require('node:os');
/* How often it looks for the check's Chrome profile. */
const POLL_MS = 50;

function profileMade() {
  return fs.readdirSync(os.tmpdir()).some((name) => name.startsWith('id1-chrome-'));
}
/* True once a fake Chrome has logged "ready" (its SIGTERM handler is in),
 * so the teardown this throw starts cannot race the fake's start. */
function fakeReady() {
  const log = process.env.HUSH_FAKE_CHROME_READY_LOG || '';
  if (!log)
    return true;
  try { return fs.readFileSync(log, 'utf8').includes('ready'); } catch (e) { return false; }
}
const timer = setInterval(() => {
  if (!profileMade() || !fakeReady())
    return;
  clearInterval(timer);
  if (process.env.HUSH_FAKE_THROW === 'reject')
    Promise.reject(new Error('fake rejection from a callback'));
  else
    throw new Error('fake throw from a callback');
}, POLL_MS);
timer.unref();
