#!/usr/bin/env python3
"""NIP-42 AUTH and private-hive gating against an isolated live relay."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIGNER = ROOT / "tests" / "sign_bip340.py"

HUMAN_SECKEY = "0000000000000000000000000000000000000000000000000000000000000001"
HUMAN_PUBKEY = "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
GUEST_SECKEY = "0000000000000000000000000000000000000000000000000000000000000003"


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

    def request(self, path, body=None, expected=200):
        payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
        token = (self.directory / "home" / "session.token").read_text().strip()
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{path}",
            data=payload,
            headers={"Content-Type": "application/json", "X-Hush-Token": token},
        )
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            assert response.status == expected, (path, response.status, expected, raw[:160])
            return json.loads(raw) if expected == 200 else raw


def sign(key, challenge, port, kind=22242, content=""):
    args = [
        sys.executable,
        str(SIGNER),
        "--key",
        key,
        "--challenge",
        challenge,
        "--relay",
        f"ws://127.0.0.1:{port}",
        "--kind",
        str(kind),
        "--content",
        content,
    ]
    if kind != 22242:
        args += ["--no-relay", "--no-challenge"]
    result = subprocess.run(args, capture_output=True, text=True, check=True)
    return json.loads(result.stdout.strip())


def connect(relay):
    sock = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
    sock.settimeout(5)
    return sock


def read_until(sock, marker, cap=262144):
    buffer = b""
    while marker not in buffer and len(buffer) < cap:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buffer += chunk
    return buffer


def drain(sock, seconds=0.3):
    sock.settimeout(seconds)
    buffer = b""
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            buffer += chunk
    except socket.timeout:
        pass
    return buffer


def find_challenge(buffer):
    marker = b'["AUTH","'
    pos = buffer.rfind(marker)
    assert pos >= 0, buffer[:200]
    return buffer[pos + len(marker):].split(b'"')[0].decode()


def take_challenge(sock):
    """Hush sends the challenge in response to the first wire frame, so a
    client that has nothing to say yet sends an empty trigger line."""
    sock.sendall(b"\n")
    return find_challenge(read_until(sock, b'["AUTH","'))


def send_event(sock, event):
    sock.sendall(b'["EVENT",' + json.dumps(event, separators=(",", ":")).encode() + b"]\n")


def check_public_hive(relay):
    """Public vibes issue a challenge but leave reads and signed writes open."""
    sock = connect(relay)
    try:
        challenge = take_challenge(sock)
        assert len(challenge) == 64, challenge
        assert all(c in "0123456789abcdef" for c in challenge), challenge
        sock.sendall(b'["REQ","pub-sub",{"kinds":[1]}]\n')
        buffer = read_until(sock, b'"EOSE"')
        assert b'"EOSE","pub-sub"' in buffer, buffer[:200]
        auth_event = sign(GUEST_SECKEY, challenge, relay.port)
        send_event(sock, auth_event)
        buffer = read_until(sock, b'"OK"')
        assert b"false" in buffer and b"restricted" in buffer, buffer[:200]
        events = relay.request("/api/events")["events"]
        assert not any(e["kind"] == 22242 for e in events)
        print("public hive: challenge issued, open REQ, 22242 restricted OK")
    finally:
        sock.close()


def check_private_gates(relay):
    """Private vibes close REQ and EVENT until a member AUTHs or JOINs."""
    session = relay.request("/api/identity", {"action": "import", "nsec": HUMAN_SECKEY})
    assert session["pubkey"] == HUMAN_PUBKEY, session.get("pubkey")
    relay.request("/api/vibe", {"name": "Authz test", "about": "Temporary"})
    session = relay.request("/api/vibe", {"visibility": "private"})
    assert session["vibe"]["visibility"] == "private"
    token = session["vibe"]["join_token"]
    assert token, session.get("vibe")

    sock = connect(relay)
    try:
        take_challenge(sock)
        sock.sendall(b'["REQ","s",{"kinds":[1]}]\n')
        buffer = read_until(sock, b'"CLOSED"')
        assert b"auth-required" in buffer, buffer[:200]
        guest_event = sign(GUEST_SECKEY, "whatever", relay.port, kind=1, content="gated hello")
        send_event(sock, guest_event)
        buffer = read_until(sock, b'"OK"')
        assert b"false" in buffer and b"auth-required" in buffer, buffer[:200]
        sock.sendall(b'["JOIN","deadbeef"]\n')
        buffer = read_until(sock, b'"NOTICE"')
        assert b"invalid" in buffer, buffer[:200]
        sock.sendall(b'["REQ","s2",{"kinds":[1]}]\n')
        buffer = read_until(sock, b'"CLOSED"')
        assert b"auth-required" in buffer, buffer[:200]
        sock.sendall(b'["JOIN","' + token.encode() + b'"]\n')
        buffer = read_until(sock, b'"NOTICE"')
        assert b"joined" in buffer, buffer[:200]
        sock.sendall(b'["REQ","s3",{"kinds":[1]}]\n')
        buffer = read_until(sock, b'"EOSE"')
        assert b'"EOSE","s3"' in buffer, buffer[:200]
        print("private hive: REQ/EVENT gated, bad/good JOIN flow OK")
    finally:
        sock.close()

    # Member AUTH: wrong challenge fails and rotates; the fresh challenge works.
    sock = connect(relay)
    try:
        take_challenge(sock)
        stale = sign(HUMAN_SECKEY, "stale-challenge", relay.port)
        sock.sendall(b'["AUTH",' + json.dumps(stale, separators=(",", ":")).encode() + b"]\n")
        stale_ok = read_until(sock, b'"OK"')
        assert b"false" in stale_ok, stale_ok[:200]
        fresh = find_challenge(stale_ok)
        assert fresh != "stale-challenge"
        auth_event = sign(HUMAN_SECKEY, fresh, relay.port)
        sock.sendall(b'["AUTH",' + json.dumps(auth_event, separators=(",", ":")).encode() + b"]\n")
        buffer = read_until(sock, b'"OK"')
        assert b"true" in buffer, buffer[:200]
        sock.sendall(b'["REQ","s4",{"kinds":[1]}]\n')
        buffer = read_until(sock, b'"EOSE"')
        assert b'"EOSE","s4"' in buffer, buffer[:200]
        other_event = sign(GUEST_SECKEY, "whatever", relay.port, kind=1, content="other key")
        send_event(sock, other_event)
        buffer = read_until(sock, b'"OK"')
        assert b"false" in buffer and b"pubkey mismatch" in buffer, buffer[:200]
        own_event = sign(HUMAN_SECKEY, "whatever", relay.port, kind=1, content="own key hello")
        send_event(sock, own_event)
        buffer = read_until(sock, b'"OK","' + own_event["id"].encode())
        assert b"true" in buffer, buffer[:200]
        print("private hive: member AUTH binds pubkey, rotation after failure OK")
    finally:
        sock.close()


def check_fanout_privacy(relay, token):
    """Only authorized connections receive private-hive fanout."""
    reader = connect(relay)
    outsider = connect(relay)
    try:
        take_challenge(reader)
        reader.sendall(b'["JOIN","' + token.encode() + b'"]\n')
        buffer = read_until(reader, b'"NOTICE"')
        assert b"joined" in buffer, buffer[:200]
        reader.sendall(b'["REQ","priv-sub",{"kinds":[1]}]\n')
        read_until(reader, b'"EOSE"')
        take_challenge(outsider)
        outsider.sendall(b'["REQ","priv-sub2",{"kinds":[1]}]\n')
        buffer = read_until(outsider, b'"CLOSED"')
        assert b"auth-required" in buffer, buffer[:200]
        relay.request("/api/event", {"content": "fanout hello"})
        buffer = read_until(reader, b"fanout hello")
        assert b'"EVENT"' in buffer, buffer[:200]
        quiet = drain(outsider)
        assert b"fanout hello" not in quiet, quiet[:200]
        print("private hive: fanout reaches authorized subscribers only OK")
    finally:
        reader.close()
        outsider.close()


def check_rotation_and_restart(relay):
    """Rotation invalidates the old token; persistence stores only the hash."""
    before = relay.request("/api/session")["vibe"]["join_token"]
    rotated = relay.request("/api/vibe", {"action": "rotate_token"})
    new_token = rotated["join_token"]
    assert new_token and new_token != before, (before, new_token)
    assert relay.request("/api/session")["vibe"]["join_token"] == new_token

    candidates = [relay.directory / "config" / "vibe.json", relay.directory / "home" / "vibe.json"]
    vibe_path = next(p for p in candidates if p.exists())
    raw = vibe_path.read_text()
    assert "vibe_token_hash" in raw, raw
    assert new_token not in raw, raw

    sock = connect(relay)
    try:
        take_challenge(sock)
        sock.sendall(b'["JOIN","' + before.encode() + b'"]\n')
        buffer = read_until(sock, b'"NOTICE"')
        assert b"invalid" in buffer, buffer[:200]
    finally:
        sock.close()

    directory = relay.directory
    relay.stop()
    restarted = Relay(directory)
    try:
        restarted.start()
        session = restarted.request("/api/session")
        assert session["vibe"]["join_token"] == "", session.get("vibe")
        sock = connect(restarted)
        try:
            take_challenge(sock)
            sock.sendall(b'["JOIN","' + new_token.encode() + b'"]\n')
            buffer = read_until(sock, b'"NOTICE"')
            assert b"joined" in buffer, buffer[:200]
        finally:
            sock.close()
        print("rotation + restart: old token dies, hash-only file, display-once OK")
    finally:
        restarted.stop()


def main():
    with tempfile.TemporaryDirectory(prefix="hush-authz-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            check_public_hive(relay)
            check_private_gates(relay)
            token = relay.request("/api/session")["vibe"]["join_token"]
            check_fanout_privacy(relay, token)
            check_rotation_and_restart(relay)
        finally:
            relay.stop()
    print("check_authz.py: all checks passed")


if __name__ == "__main__":
    main()
