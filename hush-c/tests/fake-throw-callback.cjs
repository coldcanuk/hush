'use strict';
// Preload (node --require) for tests/check_restart_ui_tmp.sh (#326). Once
// the check has made its Chrome profile, it throws from a timer callback,
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
const timer = setInterval(() => {
  if (!profileMade())
    return;
  clearInterval(timer);
  if (process.env.HUSH_FAKE_THROW === 'reject')
    Promise.reject(new Error('fake rejection from a callback'));
  else
    throw new Error('fake throw from a callback');
}, POLL_MS);
timer.unref();
