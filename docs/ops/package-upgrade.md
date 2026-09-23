# Package upgrade: graceful relay stop

Upgrading `hush-relay` no longer surprise-kills the running relay. Both
packaging paths stop it the same way: SIGTERM, then a generous poll so
agent jobs can reap. No benchmark numbers are claimed here — only the
stop behavior and how to check it.

## Behavior

| Event | Debian scriptlet | RPM scriptlet | Stop mode |
|---|---|---|---|
| Upgrade | old `prerm upgrade` (runs before new files unpack) | new `%pre` with `$1 == 2` (runs before new binary lands) | graceful, **no SIGKILL** |
| Upgrade tail | old `postrm upgrade` (no stop) | old `%preun` with `$1 == 1` | graceful, **no SIGKILL** |
| Remove / purge | `prerm remove` | `%preun` with `$1 == 0` | graceful, then **SIGKILL** as a last resort |
| Deconfigure / failed-upgrade | `prerm` | — | graceful, no SIGKILL (package stays installed) |

Graceful means: SIGTERM every `hush-relay` process (plus a best-effort
`hush-relay --quit` for the default port first), then poll once per
second for up to the grace period. SIGTERM and `--quit` share one
cleanup path in the relay (handler == `--quit` == `/api/exit`: reap
agent children, exit 0), so the broadcast SIGTERM is equivalent to
`--quit` on each live port without needing to know the ports.

## Grace period and opt-outs

- Default grace: **30s** (`HUSH_STOP_GRACE_S`, seconds, positive
  integer; garbage falls back to 30). Thirty seconds comfortably covers
  the relay's own shutdown (its per-child reap waits bounded seconds,
  and agent-job cancel has a 3s grace) with headroom, while keeping
  `dpkg`/`rpm` transactions bounded.
- Upgrade survivors are left running the old binary and reported on
  stderr; the scriptlet still exits 0 so the transaction never fails
  over a lingering process. Restart at your convenience
  (`hush-relay --quit`, then start the new build).
- `HUSH_RELAY_ALLOW_KILL=1` restores SIGKILL-after-grace on upgrade for
  operators who want the old behavior. Remove/purge always SIGKILLs
  after the grace so no stale relay survives a removal.
- A zombie (reparented, parent not reaping) still answers `kill -0`,
  so it can be reported as a survivor; the relay reaps its own
  children on SIGTERM, so this is rare and harmless.

The canonical logic is `scripts/hush-relay-stop` (installed as
`/usr/share/hush/hush-relay-stop`). The `prerm` / `%pre` / `%preun`
wrappers delegate to it and carry a small inline fallback with
identical semantics for systems upgrading from a package that predates
the helper. Note: an upgrade *from* a pre-graceful package still runs
that package's old `prerm`/blast scriptlet — the graceful path takes
effect once a graceful package is installed.

## Verify

Focused helper test (no dpkg/rpm needed in CI):

```bash
sh hush-c/tests/check_pkg_upgrade.sh
```

It asserts: the upgrade path never SIGKILLs a SIGTERM-ignoring process
inside the grace window, a SIGTERM-polite process is reaped without
any KILL, `remove` SIGKILLs after the grace, the `HUSH_RELAY_ALLOW_KILL=1`
opt-in works, and unknown modes degrade to graceful. It also statically
checks that `prerm` and the spec delegate to the helper with a generous
grace and that `%preun` distinguishes erase from upgrade. The check
runs under `make test`.

Manual upgrade smoke (needs `dpkg`/`rpm` on the target):

1. Start the relay, note its pid.
2. Install the new package version.
3. The old pid must be gone with a clean exit (no SIGKILL on upgrade);
   if it outlives the grace, it must still be running, not killed.
