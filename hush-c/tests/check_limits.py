#!/usr/bin/env python3
"""Rate-limit checks against an isolated live relay."""

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
            if response.status != expected:
                raise AssertionError((path, response.status, expected, raw[:120]))
            return json.loads(raw) if expected == 200 else raw


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


def take_challenge(sock):
    """Raw TCP: the relay sends the challenge in response to the first frame."""
    sock.sendall(b"\n")
    return read_until(sock, b'["AUTH","')


def check_event_flood(relay):
    """Per-connection EVENT bucket answers rate-limited before crypto."""
    sock = connect(relay)
    try:
        take_challenge(sock)
        junk = '["EVENT",{"id":"x","pubkey":"x","kind":1,"created_at":0,'
        junk += '"content":"","tags":[],"sig":""}]\n'
        sock.sendall((junk * 30).encode())
        buffer = read_until(sock, b"rate-limited")
        assert b"rate-limited" in buffer, buffer[-400:]
        assert b'"OK"' in buffer, buffer[-400:]
        print("wire: EVENT flood answered rate-limited OK")
    finally:
        sock.close()


def check_req_flood(relay):
    """Per-connection REQ bucket closes with rate-limited."""
    sock = connect(relay)
    try:
        take_challenge(sock)
        sock.sendall(b'["REQ","r",{"kinds":[1]}]\n' * 12)
        buffer = read_until(sock, b"rate-limited")
        assert b'"CLOSED"' in buffer and b"rate-limited" in buffer, buffer[-400:]
        print("wire: REQ flood answered CLOSED rate-limited OK")
    finally:
        sock.close()


def check_auth_attempts(relay):
    """Too many AUTH attempts drop the connection."""
    sock = connect(relay)
    try:
        take_challenge(sock)
        junk = '["AUTH",{"id":"x","pubkey":"x","kind":22242,'
        junk += '"created_at":0,"content":"","tags":[],"sig":""}]\n'
        sock.sendall((junk * 9).encode())
        sock.settimeout(5)
        got = b""
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            got += chunk
            if len(got) > 262144:
                break
        assert got.count(b'"OK"') <= 8, got[-400:]
        print("wire: AUTH attempt cap drops the connection OK")
    finally:
        sock.close()


def status_of(relay, path, body=None):
    payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
    token = (relay.directory / "home" / "session.token").read_text().strip()
    request = urllib.request.Request(
        f"http://127.0.0.1:{relay.port}{path}",
        data=payload,
        headers={"Content-Type": "application/json", "X-Hush-Token": token},
    )
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            return response.status
    except urllib.error.HTTPError as error:
        return error.code


def check_http_flood(relay):
    """Per-IP API request bucket answers 429."""
    statuses = []
    for _ in range(130):
        statuses.append(status_of(relay, "/api/session"))
        if statuses[-1] == 429:
            break
    assert 429 in statuses, statuses[-5:]
    print("http: per-IP flood answered 429 OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-limits-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            check_event_flood(relay)
            check_req_flood(relay)
            check_auth_attempts(relay)
            check_http_flood(relay)
        finally:
            relay.stop()
    print("check_limits.py: all checks passed")


if __name__ == "__main__":
    main()
