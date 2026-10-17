#!/usr/bin/env python3
"""Owner-token bootstrap (#336, Refs #322).

The relay used to hand the owner token, in Set-Cookie, to any loopback
request that carried none, and accepted it from a ?k= URL. Any local
process (an agent shell child included) could curl it out and drive the
whole API. Now:

- no response to a caller without a credential carries Set-Cookie;
- ?k=<token> is no credential (it lands in history, logs, and Referer);
- the cookie goes only to the request that redeems the one-time pairing
  code: in memory only, never the token, new each start, dead after use.

A stub local caller (a separate process with no token) probes the relay.
Stub PATH and temp HOME/HUSH_HOME; provider CLIs on PATH are canaries.
"""

import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CANARIES = ("goose", "grok", "codex", "copilot", "cline", "ollama")

# The stub local caller: raw HTTP, no credential, prints each reply head.
CALLER = r'''
import json, socket, sys
port, token = int(sys.argv[1]), sys.argv[2]
probes = [("GET", "/"), ("GET", "/index.html"), ("GET", "/api/status"),
          ("GET", "/api/session"), ("GET", "/?k=" + token),
          ("GET", "/api/session?k=" + token), ("POST", "/api/event"),
          ("POST", "/api/event?k=" + token)]
body = json.dumps({"content": "Yes", "kind": 1, "channel": "general"}).encode()
out = []
for method, path in probes:
    payload = body if method == "POST" else b""
    head = (f"{method} {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
            "Content-Type: application/json\r\n"
            f"Content-Length: {len(payload)}\r\nConnection: close\r\n\r\n")
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(head.encode() + payload)
    raw = b""
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        raw += chunk
    s.close()
    out.append([method, path, raw.decode("latin-1")])
print(json.dumps(out))
'''


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def write_canaries(bin_dir, hit_file):
    bin_dir.mkdir(exist_ok=True)
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
        self.log_path = directory / "relay.log"
        self.process = None

    def start(self):
        self.log = self.log_path.open("w")
        self.process = subprocess.Popen(
            [os.environ.get("HUSH_TEST_RELAY_BIN", str(ROOT / "hush-relay")),
             "--no-open", str(self.port)],
            cwd=ROOT, env=self.environment,
            stdout=self.log, stderr=subprocess.STDOUT,
        )
        for _ in range(100):
            try:
                if (self.directory / "home" / "session.token").exists() and \
                        status_of(self.send("GET", "/api/status", [])) == 200 and \
                        "listening on" in self.log_path.read_text():
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
            self.log.close()

    def token(self):
        return (self.directory / "home" / "session.token").read_text().strip()

    def pair_code(self):
        found = re.findall(r"\?pair=([0-9a-f]+)", self.log_path.read_text())
        assert found, "relay --no-open must print its pairing link"
        return found[-1]

    def send(self, method, path, headers, body=None):
        payload = b"" if body is None else json.dumps(body).encode()
        lines = [f"{method} {path} HTTP/1.1", f"Host: {self.host}"] + headers
        lines += ["Content-Type: application/json",
                  f"Content-Length: {len(payload)}", "Connection: close"]
        head = ("\r\n".join(lines) + "\r\n\r\n").encode()
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        try:
            sock.sendall(head + payload)
            out = b""
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                out += chunk
            return out
        finally:
            sock.close()


def status_of(raw):
    return int(raw.split(b" ")[1])


def head_of(raw):
    return raw.split(b"\r\n\r\n", 1)[0]


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


def check_stub_caller(relay):
    """A local process with no token learns nothing and changes nothing."""
    token = relay.token()
    before = note_count(relay)
    env = {"PATH": relay.environment["PATH"], "HOME": str(relay.directory / "user")}
    done = subprocess.run([sys.executable, "-c", CALLER, str(relay.port), token],
                          env=env, capture_output=True, text=True, timeout=60, check=True)
    for method, path, raw in json.loads(done.stdout):
        head = raw.split("\r\n\r\n", 1)[0]
        label = f"{method} {path.replace(token, '<token>')}"
        assert "set-cookie" not in head.lower(), (label, head[:300])
        assert token not in raw.replace(path, ""), (label, "owner token leaked")
        if path.startswith("/api/session") or method == "POST":
            assert raw.startswith("HTTP/1.1 401"), (label, head[:200])
    assert note_count(relay) == before, "an unauthenticated Yes was stored"
    print("bootstrap: stub local caller: no Set-Cookie, no token, ?k= 401, POST 401 OK")


def check_pairing(relay):
    """Only the one-time code earns the cookie, once; the cookie then works."""
    token = relay.token()
    code = relay.pair_code()
    assert len(code) == 32 and code != token, "pairing code is not the owner token"
    for path in (relay.directory / "home").rglob("*"):
        if path.is_file():
            assert code.encode() not in path.read_bytes(), f"pairing code on disk: {path}"
    near = code[:-1] + ("0" if code[-1] != "0" else "1")
    for wrong in ("0" * 32, near, code[:-1], code + "0", ""):
        raw = relay.send("GET", "/?pair=" + wrong, [])
        assert b"set-cookie" not in head_of(raw).lower(), f"code {wrong!r} earned a cookie"
    raw = relay.send("GET", "/", [f"Referer: http://{relay.host}/?pair={code}"])
    assert b"set-cookie" not in head_of(raw).lower(), "a code outside the request line counted"
    raw = relay.send("GET", "/?pair=" + code, [])
    head = head_of(raw).decode("latin-1")
    want = f"Set-Cookie: hush_session={token}; HttpOnly; SameSite=Strict; Path=/; Max-Age="
    assert want in head, head[:400]
    raw = relay.send("GET", "/?pair=" + code, [])
    assert b"set-cookie" not in head_of(raw).lower(), "a spent code earned a second cookie"
    ui = re.search(r"(?i)X-Hush-Nonce: ([0-9a-f]{32})", head)
    assert ui, "pairing sets the window's X-Hush-Nonce"
    cookie = [f"Cookie: hush_session={token}", f"X-Hush-Nonce: {ui.group(1)}",
              f"Origin: http://{relay.host}", "Sec-Fetch-Site: same-origin"]
    assert status_of(relay.send("GET", "/api/session", cookie)) == 200
    before = note_count(relay)
    yes = {"content": "paired hello", "kind": 1, "channel": "general"}
    assert status_of(relay.send("POST", "/api/event", cookie, yes)) == 200
    assert note_count(relay) == before + 1, "the paired window cannot post"
    header = [f"X-Hush-Token: {token}"]
    assert status_of(relay.send("GET", "/api/session", header)) == 200
    print("bootstrap: pairing code once -> cookie; wrong/spent code none; paired POST OK")
    return code


def main():
    with tempfile.TemporaryDirectory(prefix="hush-bootstrap-") as temporary:
        relay = Relay(Path(temporary))
        relay.start()
        try:
            sign_in(relay)
            check_stub_caller(relay)
            first = check_pairing(relay)
            relay.stop()
            relay.start()
            assert relay.pair_code() != first, "a restart must mint a new pairing code"
            raw = relay.send("GET", "/?pair=" + first, [])
            assert b"set-cookie" not in head_of(raw).lower(), "old code survived a restart"
            check_stub_caller(relay)
            print("bootstrap: restart mints a new code; the old one is dead OK")
        finally:
            relay.stop()
        assert not relay.hits.exists(), relay.hits.read_text()
    print("bootstrap: provider canaries silent OK")


if __name__ == "__main__":
    main()
