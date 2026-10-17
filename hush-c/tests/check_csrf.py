#!/usr/bin/env python3
"""Origin / CSRF gate on state-changing API routes (P1, Refs #322).

A browser attaches the hush_session cookie (or a ?k= link) to any request,
including a cross-site POST. Every POST under /api/ therefore needs:

- Sec-Fetch-Site, when sent, equal to same-origin;
- Origin, when sent, exactly http://<Host> of the request;
- no Origin only for a header-credential client (X-Hush-Token or Bearer),
  which a browser cannot send cross-site without a preflight the relay
  never grants.

Reads (GET) are unchanged. Runs a relay with a stub PATH and temp
HOME/HUSH_HOME; any provider CLI on PATH is a canary that must stay silent.
"""

import json
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CANARIES = ("goose", "grok", "codex", "copilot", "cline", "ollama")


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def write_canaries(bin_dir, hit_file):
    bin_dir.mkdir()
    for name in CANARIES:
        stub = bin_dir / name
        stub.write_text(f'#!/bin/sh\necho "{name} $*" >> "{hit_file}"\nexit 97\n')
        stub.chmod(0o755)


class Relay:
    def __init__(self, directory):
        self.directory = directory
        self.port = free_port()
        self.host = f"127.0.0.1:{self.port}"
        self.hits = directory / "canary.log"
        write_canaries(directory / "bin", self.hits)
        self.environment = dict(
            os.environ,
            HOME=str(directory / "user"),
            HUSH_HOME=str(directory / "home"),
            HUSH_CONFIG_DIR=str(directory / "config"),
            PASSWORD_STORE_DIR=str(directory / "pass"),
            PATH=str(directory / "bin") + os.pathsep + os.environ.get("PATH", ""),
            HUSH_AUTO_UPDATE="0",
        )
        self.log = (directory / "relay.log").open("w+")
        self.process = None

    def start(self):
        self.process = subprocess.Popen(
            [os.environ.get("HUSH_TEST_RELAY_BIN", str(ROOT / "hush-relay")),
             "--no-open", str(self.port)],
            cwd=ROOT, env=self.environment,
            stdout=self.log, stderr=subprocess.STDOUT,
        )
        for _ in range(100):
            try:
                if (self.directory / "home" / "session.token").exists() and \
                        status_of(self.send("GET", "/api/status", [])) == 200:
                    return
            except OSError:
                pass
            time.sleep(0.05)
        raise AssertionError("Isolated relay did not start")

    def stop(self):
        if self.process is not None:
            self.process.terminate()
            self.process.wait(timeout=5)
            self.process = None

    def token(self):
        return (self.directory / "home" / "session.token").read_text().strip()

    def send(self, method, path, headers, body=None, host=None):
        payload = b"" if body is None else json.dumps(body).encode()
        lines = [f"{method} {path} HTTP/1.1", f"Host: {host or self.host}"]
        lines += headers
        lines += ["Content-Type: application/json",
                  f"Content-Length: {len(payload)}", "Connection: close"]
        head = ("\r\n".join(lines) + "\r\n\r\n").encode()
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        try:
            sock.sendall(head + payload)
            out = b""
            sock.settimeout(3)
            try:
                while True:
                    chunk = sock.recv(65536)
                    if not chunk:
                        break
                    out += chunk
            except socket.timeout:
                pass
            return out
        finally:
            sock.close()


def status_of(raw):
    return int(raw.split(b" ")[1])


def note_count(relay):
    raw = relay.send("GET", "/api/events", [f"X-Hush-Token: {relay.token()}"])
    assert status_of(raw) == 200, raw[:160]
    events = json.loads(raw.split(b"\r\n\r\n", 1)[1])["events"]
    return sum(1 for e in events if e.get("kind") == 1)


def sign_in(relay):
    header = [f"X-Hush-Token: {relay.token()}"]
    raw = relay.send("POST", "/api/identity", header, {"action": "create"})
    assert status_of(raw) == 200, raw[:200]
    raw = relay.send("POST", "/api/identity", header,
                     {"action": "ack_backup", "save_pass": False})
    assert status_of(raw) == 200, raw[:200]


def check_refused(relay):
    """Every browser-shaped forgery of an owner "Yes" is refused, not stored."""
    token = relay.token()
    served = f"Origin: http://{relay.host}"
    yes = {"content": "Yes", "kind": 1, "channel": "general"}
    cases = [
        ([f"Cookie: hush_session={token}", "Origin: https://evil.example"], "/api/event",
         "cookie + foreign Origin"),
        ([f"Cookie: hush_session={token}"], "/api/event", "cookie + no Origin"),
        ([f"Cookie: hush_session={token}", "Origin: null"], "/api/event", "cookie + Origin null"),
        ([f"Cookie: hush_session={token}", served, "Sec-Fetch-Site: cross-site"], "/api/event",
         "cookie + served Origin + Sec-Fetch-Site cross-site"),
        ([f"Cookie: hush_session={token}", served, "Sec-Fetch-Site: same-site"], "/api/event",
         "cookie + Sec-Fetch-Site same-site"),
        ([f"Cookie: hush_session={token}", f"Origin: http://{relay.host}.evil.example"],
         "/api/event", "cookie + served Origin as a prefix"),
        ([f"Cookie: hush_session={token}", f"Origin: https://{relay.host}"], "/api/event",
         "cookie + https scheme"),
        ([], f"/api/event?k={token}", "?k= link + no Origin"),
        ([f"X-Hush-Token: {token}", "Origin: https://evil.example"], "/api/event",
         "header token + foreign Origin"),
        ([f"Authorization: Bearer {token}", "Sec-Fetch-Site: cross-site"], "/api/event",
         "bearer + Sec-Fetch-Site cross-site"),
    ]
    before = note_count(relay)
    for headers, path, label in cases:
        raw = relay.send("POST", path, headers, yes)
        assert status_of(raw) == 403, (label, raw[:200])
        assert b"cross-origin request refused" in raw, (label, raw[:200])
    raw = relay.send("POST", "/api/profile", [f"Cookie: hush_session={token}",
                     "Origin: https://evil.example"], {"approval_mode": "auto_approve"})
    assert status_of(raw) == 403, ("profile cross-origin", raw[:200])
    assert note_count(relay) == before, "a refused POST stored a note"
    print("csrf: foreign/missing/null Origin, cross-site fetch, ?k= POST all 403, nothing stored OK")


def check_allowed(relay):
    """The served UI, header clients and plain reads keep working."""
    token = relay.token()
    yes = {"content": "Yes", "kind": 1, "channel": "general"}
    before = note_count(relay)
    ui = [f"Cookie: hush_session={token}", f"Origin: http://{relay.host}",
          "Sec-Fetch-Site: same-origin"]
    assert status_of(relay.send("POST", "/api/event", ui, yes)) == 200, "UI POST"
    local = f"localhost:{relay.port}"
    raw = relay.send("POST", "/api/event", [f"Cookie: hush_session={token}",
                     f"Origin: http://{local}"], yes, host=local)
    assert status_of(raw) == 200, ("localhost origin", raw[:200])
    raw = relay.send("POST", "/api/event", [f"X-Hush-Token: {token}"], yes)
    assert status_of(raw) == 200, ("header client", raw[:200])
    raw = relay.send("POST", "/api/event", [f"Authorization: Bearer {token}",
                     f"Origin: http://{relay.host}"], yes)
    assert status_of(raw) == 200, ("bearer + served origin", raw[:200])
    assert note_count(relay) == before + 4, "allowed POSTs did not all land"
    raw = relay.send("GET", "/api/session", [f"Cookie: hush_session={token}",
                     "Origin: https://evil.example", "Sec-Fetch-Site: cross-site"])
    assert status_of(raw) == 200, ("GET stays a read", raw[:200])
    raw = relay.send("POST", "/api/event", ["Origin: https://evil.example"], yes)
    assert status_of(raw) == 401, ("no token still 401 first", raw[:200])
    print("csrf: same-origin UI, localhost, header/bearer clients 200; GET read 200; no token 401 OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-csrf-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            sign_in(relay)
            check_refused(relay)
            check_allowed(relay)
        finally:
            relay.stop()
        assert not relay.hits.exists(), relay.hits.read_text()
    print("check_csrf.py: all checks passed (canary 0)")


if __name__ == "__main__":
    main()
