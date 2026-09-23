#!/usr/bin/env python3
"""HTTP trust-boundary regression suite for an isolated live relay.

Locks in behaviors that must not regress silently:

- session-token carriers: Cookie / X-Hush-Token / Bearer / ?k= all open
  the gate; a wrong or missing token answers 401.
- Host allowlist: loopback names (with or without port) are served;
  foreign or missing Host answers 403, including on /api/status.
- No CORS: no reply carries Access-Control-Allow-Origin, and OPTIONS
  answers 204 without one.
- Privileged GET/POST behind the token: /api/ice and /api/provider/scan
  answer 401 without a credential; an authenticated scan fails closed on
  an unreachable host without echoing the submitted key, and /api/ice
  with TURN idle exposes only the public STUN entry (no credential).
"""

import json
import os
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class Relay:
    def __init__(self, directory):
        self.directory = directory
        self.port = free_port()
        self.environment = dict(
            os.environ,
            HUSH_HOME=str(directory / "home"),
            HUSH_CONFIG_DIR=str(directory / "config"),
            PASSWORD_STORE_DIR=str(directory / "pass"),
            HUSH_AUTO_UPDATE="0",
        )
        self.log = (directory / "relay.log").open("w+")
        self.process = None

    def start(self):
        self.process = subprocess.Popen(
            [
                os.environ.get("HUSH_TEST_RELAY_BIN", str(ROOT / "hush-relay")),
                "--no-open",
                str(self.port),
            ],
            cwd=ROOT,
            env=self.environment,
            stdout=self.log,
            stderr=subprocess.STDOUT,
        )
        for _ in range(100):
            try:
                self.request("/api/session")
                return
            except (OSError, ValueError):
                time.sleep(0.05)
        raise AssertionError("Isolated relay did not start")

    def stop(self):
        if self.process is not None:
            self.process.terminate()
            self.process.wait(timeout=5)
            self.process = None

    def token(self):
        return (self.directory / "home" / "session.token").read_text().strip()

    def request(self, path, body=None, expected=200):
        payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}",
            data=payload,
            headers={"Content-Type": "application/json", "X-Hush-Token": self.token()},
        )
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            assert response.status == expected, (path, response.status, expected, raw[:160])
            return json.loads(raw) if expected == 200 else raw


def raw_request(relay, head, body=b""):
    """Sends a hand-built request so Host/CORS headers stay under test control."""
    sock = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
    try:
        sock.sendall(head + body)
        sock.settimeout(2)
        out = b""
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


def headers_of(raw):
    return raw.split(b"\r\n\r\n")[0]


def check_token_carriers(relay):
    token = relay.token()
    assert len(token) == 32, token
    base = f"127.0.0.1:{relay.port}".encode()
    cases = [
        (b"X-Hush-Token: " + token.encode(), "header token"),
        (b"Authorization: Bearer " + token.encode(), "bearer"),
        (b"Cookie: hush_session=" + token.encode(), "cookie"),
    ]
    for header, label in cases:
        raw = raw_request(
            relay,
            b"GET /api/session HTTP/1.1\r\nHost: " + base + b"\r\n" + header
            + b"\r\nConnection: close\r\n\r\n",
        )
        assert status_of(raw) == 200, (label, raw[:160])
    raw = raw_request(
        relay,
        b"GET /api/session?k=" + token.encode() + b" HTTP/1.1\r\nHost: " + base
        + b"\r\nConnection: close\r\n\r\n",
    )
    assert status_of(raw) == 200, ("query token", raw[:160])
    raw = raw_request(
        relay,
        b"GET /api/session HTTP/1.1\r\nHost: " + base
        + b"\r\nConnection: close\r\n\r\n",
    )
    assert status_of(raw) == 401, ("missing token", raw[:160])
    raw = raw_request(
        relay,
        b"GET /api/session HTTP/1.1\r\nHost: " + base
        + b"\r\nX-Hush-Token: deadbeef\r\nConnection: close\r\n\r\n",
    )
    assert status_of(raw) == 401, ("wrong token", raw[:160])
    print("trust: Cookie / X-Hush-Token / Bearer / ?k= open, wrong/missing 401 OK")


def check_host_allowlist(relay):
    token = relay.token().encode()
    good = [
        f"127.0.0.1:{relay.port}",
        "127.0.0.1",
        f"localhost:{relay.port}",
        "localhost",
    ]
    for host in good:
        raw = raw_request(
            relay,
            f"GET /api/status HTTP/1.1\r\nHost: {host}\r\n"
            "Connection: close\r\n\r\n".encode(),
        )
        assert status_of(raw) == 200, (host, raw[:160])
    bad = ["evil.com", "127.0.0.1.evil.com", "evil.com:80"]
    for host in bad:
        raw = raw_request(
            relay,
            f"GET /api/status HTTP/1.1\r\nHost: {host}\r\n"
            "Connection: close\r\n\r\n".encode(),
        )
        assert status_of(raw) == 403, (host, raw[:160])
    raw = raw_request(relay, b"GET /api/status HTTP/1.1\r\nConnection: close\r\n\r\n")
    assert status_of(raw) == 403, ("missing Host", raw[:160])
    # The allowlist runs before auth: a valid token on a foreign Host still fails.
    raw = raw_request(
        relay,
        b"GET /api/session HTTP/1.1\r\nHost: evil.com\r\nX-Hush-Token: " + token
        + b"\r\nConnection: close\r\n\r\n",
    )
    assert status_of(raw) == 403, ("authed foreign Host", raw[:160])
    print("trust: Host allowlist serves loopback, 403s foreign/missing OK")


def check_no_cors(relay):
    token = relay.token().encode()
    base = f"127.0.0.1:{relay.port}".encode()
    samples = [
        b"GET /api/status HTTP/1.1\r\nHost: " + base + b"\r\nConnection: close\r\n\r\n",
        b"GET /api/session HTTP/1.1\r\nHost: " + base + b"\r\nConnection: close\r\n\r\n",
        b"GET /api/session HTTP/1.1\r\nHost: " + base + b"\r\nX-Hush-Token: " + token
        + b"\r\nConnection: close\r\n\r\n",
        b"GET /api/status HTTP/1.1\r\nHost: evil.com\r\nConnection: close\r\n\r\n",
        b"OPTIONS /api/session HTTP/1.1\r\nHost: " + base
        + b"\r\nOrigin: https://evil.example\r\nConnection: close\r\n\r\n",
        b"GET /api/session HTTP/1.1\r\nHost: " + base + b"\r\nOrigin: https://evil.example\r\n"
        + b"X-Hush-Token: " + token + b"\r\nConnection: close\r\n\r\n",
    ]
    for sample in samples:
        raw = raw_request(relay, sample)
        assert b"Access-Control-Allow-Origin" not in headers_of(raw), sample[:80]
    options = raw_request(
        relay,
        b"OPTIONS /api/session HTTP/1.1\r\nHost: " + base
        + b"\r\nOrigin: https://evil.example\r\nConnection: close\r\n\r\n",
    )
    assert status_of(options) == 204, options[:160]
    print("trust: no Access-Control-Allow-Origin on 200/401/403/204, OPTIONS 204 OK")


def check_scan_and_ice_gates(relay):
    token = relay.token()
    base = f"127.0.0.1:{relay.port}"

    def unauthed(method, path, body=None):
        payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
        sock = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
        try:
            head = (
                f"{method} {path} HTTP/1.1\r\nHost: {base}\r\n"
                "Content-Type: application/json\r\n"
                f"Content-Length: {len(payload) if payload else 0}\r\n"
                "Connection: close\r\n\r\n"
            ).encode()
            sock.sendall(head + (payload or b""))
            sock.settimeout(2)
            out = b""
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

    assert status_of(unauthed("GET", "/api/ice")) == 401, "ice without token"
    scan_body = {"provider": "openai-api", "host": "http://127.0.0.1:1"}
    assert status_of(unauthed("POST", "/api/provider/scan", scan_body)) == 401, \
        "scan without token"

    key = "sk-trust-probe-secret"
    scan = relay.request(
        "/api/provider/scan",
        {"provider": "openai-api", "host": "http://127.0.0.1:1", "api_key": key},
    )
    assert scan["ok"] is False, scan
    sock_body = json.dumps(scan, separators=(",", ":")).encode()
    assert key.encode() not in sock_body, "scan echoed the submitted key"

    ice = relay.request("/api/ice")
    assert ice["ok"] is True, ice
    assert ice.get("running") is False, ice
    blob = json.dumps(ice, separators=(",", ":")).encode()
    assert b"credential" not in blob, ice
    assert b"stun:stun.l.google.com:19302" in blob, ice
    print("trust: /api/ice + /api/provider/scan gated, scan fail-closed, no leaks OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-trust-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            check_token_carriers(relay)
            check_host_allowlist(relay)
            check_no_cors(relay)
            check_scan_and_ice_gates(relay)
        finally:
            relay.stop()
    print("check_trust.py: all checks passed")


if __name__ == "__main__":
    main()
