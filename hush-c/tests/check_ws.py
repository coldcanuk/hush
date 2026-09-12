#!/usr/bin/env python3
"""WebSocket Nostr transport checks against an isolated live relay."""

import base64
import hashlib
import json
import os
import socket
import struct
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
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


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


class WsClient:
    """Minimal RFC 6455 client: handshake, masked writes, unmasked reads."""

    def __init__(self, relay):
        self.sock = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
        self.sock.settimeout(5)
        self.buffer = b""
        self.mask_key = os.urandom(4)

    def handshake(self):
        key = base64.b64encode(os.urandom(16)).decode()
        expected = base64.b64encode(
            hashlib.sha1((key + GUID).encode()).digest()
        ).decode()
        request = (
            "GET / HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        self.sock.sendall(request.encode())
        raw = self._read_until(b"\r\n\r\n")
        end = raw.find(b"\r\n\r\n") + 4
        head = raw[:end].decode(errors="replace")
        self.buffer = raw[end:]
        assert "101 Switching Protocols" in head, head[:200]
        assert f"Sec-WebSocket-Accept: {expected}" in head, head[:200]
        return key

    def _read_until(self, marker):
        while marker not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            self.buffer += chunk
        return self.buffer

    def read_frame(self):
        """Returns (opcode, payload) of the next complete server frame."""
        while True:
            frame = self._try_frame()
            if frame is not None:
                return frame
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("socket closed")
            self.buffer += chunk

    def _try_frame(self):
        if len(self.buffer) < 2:
            return None
        b0, b1 = self.buffer[0], self.buffer[1]
        assert (b1 & 0x80) == 0, "server frames must be unmasked"
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            if len(self.buffer) < 4:
                return None
            length = struct.unpack(">H", self.buffer[2:4])[0]
            offset = 4
        elif length == 127:
            if len(self.buffer) < 10:
                return None
            length = struct.unpack(">Q", self.buffer[2:10])[0]
            offset = 10
        if len(self.buffer) < offset + length:
            return None
        payload = self.buffer[offset:offset + length]
        self.buffer = self.buffer[offset + length:]
        return b0 & 0x0F, payload

    def read_text(self):
        opcode, payload = self.read_frame()
        assert opcode == 1, (opcode, payload[:120])
        return payload.decode()

    def send_frame(self, opcode, payload=b"", fin=True, masked=True):
        b0 = (0x80 if fin else 0) | (opcode & 0x0F)
        length = len(payload)
        if length < 126:
            header = struct.pack(">BB", b0, length | (0x80 if masked else 0))
        elif length <= 0xFFFF:
            header = struct.pack(">BBH", b0, 126 | (0x80 if masked else 0), length)
        else:
            header = struct.pack(">BBQ", b0, 127 | (0x80 if masked else 0), length)
        if masked:
            masked_payload = bytes(
                byte ^ self.mask_key[i % 4] for i, byte in enumerate(payload)
            )
            self.sock.sendall(header + self.mask_key + masked_payload)
        else:
            self.sock.sendall(header + payload)

    def send_text(self, text, fin=True, masked=True):
        self.send_frame(1, text.encode(), fin=fin, masked=masked)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


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


def take_challenge(ws):
    line = ws.read_text()
    assert line.startswith('["AUTH","'), line[:120]
    return line.split('"')[3]


def read_until_text(ws, marker, cap=64):
    """Accumulates text frames until marker appears (events precede EOSE)."""
    acc = ""
    for _ in range(cap):
        acc += ws.read_text()
        if marker in acc:
            return acc
    return acc


def check_handshake_and_req(relay):
    ws = WsClient(relay)
    try:
        ws.handshake()
        challenge = take_challenge(ws)
        assert len(challenge) == 64, challenge
        ws.send_text('["REQ","ws-sub",{"kinds":[1]}]\n')
        eose = read_until_text(ws, '"EOSE","ws-sub"')
        assert '"EOSE","ws-sub"' in eose, eose[:200]
        ws.send_frame(9, b"probe")
        opcode, payload = ws.read_frame()
        assert opcode == 10 and payload == b"probe", (opcode, payload)
        print("ws: handshake, immediate challenge, REQ/EOSE, ping/pong OK")
    finally:
        ws.close()


def check_auth_and_gating(relay):
    session = relay.request("/api/identity", {"action": "import", "nsec": HUMAN_SECKEY})
    assert session["pubkey"] == HUMAN_PUBKEY
    relay.request("/api/vibe", {"name": "WS test", "about": "Temporary"})
    session = relay.request("/api/vibe", {"visibility": "private"})
    token = session["vibe"]["join_token"]
    assert token

    ws = WsClient(relay)
    try:
        ws.handshake()
        take_challenge(ws)
        ws.send_text('["REQ","gated",{"kinds":[1]}]\n')
        closed = ws.read_text()
        assert '"CLOSED"' in closed and "auth-required" in closed, closed[:200]
        ws.send_text('["JOIN","' + token + '"]\n')
        notice = ws.read_text()
        assert "joined" in notice, notice[:200]
        ws.send_text('["REQ","joined-sub",{"kinds":[1]}]\n')
        eose = read_until_text(ws, '"EOSE","joined-sub"')
        assert '"EOSE","joined-sub"' in eose, eose[:200]
        print("ws: private-hive CLOSED and JOIN flow OK")
    finally:
        ws.close()

    ws = WsClient(relay)
    try:
        ws.handshake()
        challenge = take_challenge(ws)
        auth_event = sign(HUMAN_SECKEY, challenge, relay.port)
        ws.send_text('["AUTH",' + json.dumps(auth_event, separators=(",", ":")) + ']\n')
        ok = ws.read_text()
        assert '"OK","' + auth_event["id"] + '",true' in ok, ok[:200]
        ws.send_text('["REQ","authed-sub",{"kinds":[1]}]\n')
        eose = read_until_text(ws, '"EOSE","authed-sub"')
        assert '"EOSE","authed-sub"' in eose, eose[:200]
        print("ws: NIP-42 AUTH over WebSocket OK")
    finally:
        ws.close()


def check_fragmentation_and_errors(relay):
    ws = WsClient(relay)
    try:
        ws.handshake()
        take_challenge(ws)
        first = b'["REQ","frag-sub",{"kinds":[1]}]\n'
        cut = len(first) // 2
        ws.send_frame(1, first[:cut], fin=False)
        ws.send_frame(0, first[cut:], fin=True)
        # The hive is private at this point, so the reassembled REQ earns a
        # CLOSED — the reply proves the relay read the full fragmented line.
        closed = read_until_text(ws, '"CLOSED","frag-sub"')
        assert '"CLOSED","frag-sub"' in closed and "auth-required" in closed, \
            closed[:200]
        print("ws: fragmented text message reassembled OK")
    finally:
        ws.close()

    ws = WsClient(relay)
    try:
        ws.handshake()
        take_challenge(ws)
        ws.send_frame(1, b"[]", masked=False)
        opcode, payload = ws.read_frame()
        assert opcode == 8 and len(payload) == 2, (opcode, payload)
        assert struct.unpack(">H", payload)[0] == 1002, payload
        print("ws: unmasked client frame closes 1002 OK")
    finally:
        ws.close()

    ws = WsClient(relay)
    try:
        ws.handshake()
        take_challenge(ws)
        ws.send_frame(2, b"\x00\x01")
        opcode, payload = ws.read_frame()
        assert opcode == 8 and struct.unpack(">H", payload)[0] == 1003, (opcode, payload)
        print("ws: binary frame closes 1003 OK")
    finally:
        ws.close()

    ws = WsClient(relay)
    try:
        ws.handshake()
        take_challenge(ws)
        ws.send_frame(8, struct.pack(">H", 1000))
        opcode, payload = ws.read_frame()
        assert opcode == 8, (opcode, payload)
        assert struct.unpack(">H", payload)[0] == 1000, payload
        print("ws: close handshake echoes 1000 OK")
    finally:
        ws.close()


def main():
    with tempfile.TemporaryDirectory(prefix="hush-ws-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            check_handshake_and_req(relay)
            check_auth_and_gating(relay)
            check_fragmentation_and_errors(relay)
        finally:
            relay.stop()
    print("check_ws.py: all checks passed")


if __name__ == "__main__":
    main()
