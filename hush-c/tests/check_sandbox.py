#!/usr/bin/env python3
"""Agent children run jailed (#336 parts 1 and 2, Refs #322).

Q2: Landlock ABI>=4 preferred; bwrap --unshare-pid --proc /proc + no_new_privs
fallback; neither -> refuse with a clear reason. All three branches are tested
via HUSH_SANDBOX_BACKEND.

Q3: child gets a stub per-job HOME (0700). Only allowlisted provider auth paths
are visible; ~/.password-store, ~/.gnupg, ~/.ssh, $HUSH_HOME never are. An
unknown auth path refuses the turn and names the path.
"""

import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_approve_nonce import Hive, raise_happy  # noqa: E402

PASS = "kp-canary-7c1e9d"
SECRETS = {"HUSH_KEY_PASS": PASS, "HUSH_OP_VAULT": "op-canary-vault",
           "HUSH_OP_HELPER": "/nonexistent/op", "AWS_SECRET_ACCESS_KEY": "aws-canary-4f2"}
ALLOWED_PREFIXES = ("LC_", "GROK_", "CODEX_", "COPILOT_", "GOOSE_", "OLLAMA_", "CLINE_")
ALLOWED_NAMES = {"PATH", "HOME", "LANG", "LANGUAGE", "TERM", "COLORTERM", "NO_COLOR", "TZ",
                 "USER", "LOGNAME", "SHELL", "TMPDIR", "PWD", "OLDPWD", "SHLVL", "_",
                 "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
                 "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "ALL_PROXY",
                 "http_proxy", "https_proxy", "no_proxy", "all_proxy",
                 "SSL_CERT_FILE", "SSL_CERT_DIR", "NODE_EXTRA_CA_CERTS", "REQUESTS_CA_BUNDLE",
                 "CURL_CA_BUNDLE", "XAI_API_KEY", "OPENAI_API_KEY", "OPENAI_BASE_URL",
                 "ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY", "OPENROUTER_API_KEY",
                 "GH_TOKEN", "GITHUB_TOKEN", "GH_HOST"}
PROBE = Path(__file__).resolve().parent / "check_sandbox_probe.py"


def write_stub(bin_dir, report, home, cfg, port, ppid, real_home, cwd):
    del cwd
    grok = bin_dir / "grok"
    grok.write_text(
        "#!/bin/sh\n"
        f'exec {sys.executable} "{PROBE}" "{report}" "{home}" "{cfg}" '
        f'{port} {ppid} "{real_home}"\n'
    )
    grok.chmod(0o755)

def env_ok(env):
    for name in SECRETS:
        assert name not in env, name
    for name in env:
        if name.startswith("HUSH_"):
            raise AssertionError("HUSH_* leaked: " + name)
        if name in ALLOWED_NAMES:
            continue
        if any(name.startswith(p) for p in ALLOWED_PREFIXES):
            continue
        raise AssertionError("env not on allowlist: " + name)


def denied(label, value):
    assert value != "ok", label + " was allowed: " + value


def plant_sensitive(real_home: Path):
    for rel, data in ((".ssh/id_canary", "ssh-canary"),
                      (".gnupg/trustdb.gpg", "gpg-canary"),
                      (".password-store/canary.gpg", "pass-canary"),
                      (".grok/auth.json", '{"token":"grok-auth-canary"}')):
        path = real_home / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data)
        path.chmod(0o600)


def check_relay_env(hive):
    pid = hive.relay.process.pid
    text = Path(f"/proc/{pid}/environ").read_bytes().decode("latin-1", "replace")
    assert PASS not in text, "relay kept the vault phrase in /proc/environ"
    assert "HUSH_KEY_PASS=" not in text, "relay kept HUSH_KEY_PASS="
    print("sandbox: relay /proc/<pid>/environ has no HUSH_KEY_PASS OK")


def wait_report(hive, report):
    for _ in range(200):
        if report.exists() and any("sandbox stub reply" in (e.get("content") or "")
                                   for e in hive.events()):
            return json.loads(report.read_text())
        time.sleep(0.05)
    raise AssertionError("the stub never wrote its report; log=\n" +
                         hive.relay.log_path.read_text()[-2000:])


def wait_reason(hive, before, pred):
    for _ in range(200):
        for e in hive.events()[before:]:
            c = e.get("content") or ""
            if pred(c):
                return c
        time.sleep(0.05)
    return None


def open_hive(temporary, **env):
    hive = Hive(Path(temporary))
    hive.relay.environment.update(SECRETS)
    hive.relay.environment.update(env)
    plant_sensitive(Path(hive.relay.environment["HOME"]))
    hive.relay.start()
    return hive


def check_child(backend):
    with tempfile.TemporaryDirectory(prefix="hush-sandbox-") as temporary:
        hive = open_hive(temporary, HUSH_SANDBOX_BACKEND=backend)
        try:
            check_relay_env(hive)
            npub = raise_happy(hive)
            cwd = hive.relay.directory / "config" / "agent-cwd"
            cwd.mkdir(parents=True, exist_ok=True)
            report = cwd / "probe.json"
            real_home = Path(hive.relay.environment["HOME"])
            write_stub(hive.relay.directory / "bin", report,
                       hive.relay.directory / "home", hive.relay.directory / "config",
                       hive.relay.port, hive.relay.process.pid, str(real_home), cwd)
            hive.pair()
            note = {"content": f"nostr:{npub} probe the jail", "kind": 1,
                    "channel": "general", "mention_0": npub}
            st, body = hive.post("/api/event", note, hive.paired())
            assert st == 200, body[:200]
            r = wait_report(hive, report)
            env_ok(r["env"])
            for label in ("token", "vault", "vibe"):
                denied("read " + label, r["reads"][label])
            assert r["reads"]["etc"] == "ok", r["reads"]["etc"]
            # Host view: denied trees must not gain files (bwrap can "succeed" on a
            # private tmpfs path that never touches the real tree).
            assert not (hive.relay.directory / "home" / "planted").exists()
            assert not (hive.relay.directory / "config" / "planted").exists()
            if backend == "landlock":
                for label in ("home", "cfg"):
                    denied("write " + label, r["writes"][label])
            assert r["writes"]["cwd"] == "ok", r["writes"]["cwd"]
            denied("connect relay", r["connect"]["relay"])
            other = r["connect"]["other"]
            if backend == "landlock":
                assert other == "ok" or other.startswith(("TimeoutError", "ConnectionRefusedError",
                                                          "OSError")), other
            else:
                # bwrap --unshare-net: no sockets at all.
                denied("connect other", other)
            for name in ("environ", "mem", "fd"):
                denied("proc/" + name, r["proc"][name])
            assert r["ptrace"] != "ok", r["ptrace"]
            assert not any(p == hive.relay.port for _, p in r.get("leaked_socks") or []), \
                r.get("leaked_socks")
            assert r["home"]["is_stub"], r["home"]
            assert r["home"]["mode"] == oct(0o700), r["home"]
            for label in ("ssh", "gnupg", "pass"):
                denied("sensitive " + label, r["sensitive"][label])
            assert r["sensitive"]["grok_auth"] == "ok", r["sensitive"]
            print(f"sandbox[{backend}]: stub HOME 0700; auth allow; "
                  "ssh/gpg/pass denied; /proc+ptrace denied OK")
        finally:
            hive.relay.stop()
        assert not hive.relay.hits.exists(), hive.relay.hits.read_text()


def check_refuse(backend_env, pred, label):
    with tempfile.TemporaryDirectory(prefix="hush-sandbox-") as temporary:
        hive = open_hive(temporary, **backend_env)
        try:
            npub = raise_happy(hive)
            cwd = hive.relay.directory / "config" / "agent-cwd"
            cwd.mkdir(parents=True, exist_ok=True)
            write_stub(hive.relay.directory / "bin", cwd / "x.json",
                       hive.relay.directory / "home", hive.relay.directory / "config",
                       hive.relay.port, hive.relay.process.pid,
                       hive.relay.environment["HOME"], cwd)
            hive.pair()
            before = len(hive.events())
            note = {"content": f"nostr:{npub} should refuse", "kind": 1,
                    "channel": "general", "mention_0": npub}
            st, body = hive.post("/api/event", note, hive.paired())
            assert st == 200, body[:200]
            reason = wait_reason(hive, before, pred)
            assert reason, f"{label}: no clear reason; log=\n" + hive.relay.log_path.read_text()[-2000:]
            print(f"sandbox[{label}]: refused with clear reason OK")
        finally:
            hive.relay.stop()


def check_path_refuse(rel, label):
    with tempfile.TemporaryDirectory(prefix="hush-sandbox-") as temporary:
        hive = open_hive(temporary, HUSH_SANDBOX_BACKEND="landlock")
        try:
            path = Path(hive.relay.environment["HOME"]) / rel
            path.mkdir(parents=True, exist_ok=True)
            if label == "unknown-auth":
                (path / "x").write_text("x")
            hive.relay.stop()
            hive.relay.environment["HUSH_SANDBOX_EXTRA_AUTH"] = str(path)
            hive.relay.start()
            npub = raise_happy(hive)
            cwd = hive.relay.directory / "config" / "agent-cwd"
            cwd.mkdir(parents=True, exist_ok=True)
            write_stub(hive.relay.directory / "bin", cwd / "x.json",
                       hive.relay.directory / "home", hive.relay.directory / "config",
                       hive.relay.port, hive.relay.process.pid,
                       hive.relay.environment["HOME"], cwd)
            hive.pair()
            before = len(hive.events())
            note = {"content": f"nostr:{npub} forbid {label}", "kind": 1,
                    "channel": "general", "mention_0": npub}
            st, body = hive.post("/api/event", note, hive.paired())
            assert st == 200, body[:200]
            resolved = str(path.resolve())
            reason = wait_reason(
                hive, before,
                lambda c, p=resolved: "unknown auth path" in c and p in c)
            assert reason, f"{label} not refused; log=\n" + hive.relay.log_path.read_text()[-2000:]
            print(f"sandbox: {label} refused and listed OK")
        finally:
            hive.relay.stop()


def main():
    check_child("landlock")
    check_child("bwrap")
    check_refuse({"HUSH_SANDBOX_BACKEND": "none"},
                 lambda c: "Landlock" in c or "bwrap" in c,
                 "none")
    for rel, label in ((".evil-auth", "unknown-auth"),
                       (".ssh", "ssh"), (".gnupg", "gnupg"),
                       (".password-store", "password-store")):
        check_path_refuse(rel, label)
    print("sandbox: provider canaries silent OK")


if __name__ == "__main__":
    main()
