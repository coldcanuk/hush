#!/usr/bin/env python3
"""Issue #235: create/update with save_pass must not report plain OK on pass fail.

Pins HTTP 400 reasons (missing|fail) and session pass_error. Update with
save_pass:true must store agents/<slug>/nsec. Path is pinned in test_save_pass.c.
"""

from __future__ import annotations

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

PASS_MISSING = "Could not save the robot's key to pass: pass is not available."
PASS_FAIL = "Could not save the robot's key to pass: pass helper failed."
PASS_PATH = "Could not save the robot's key to pass: path is too long."

FAILURES: list[str] = []


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Relay:
    def __init__(self, directory: Path, pass_helper: str, fake_dir: Path | None = None):
        self.directory = directory
        self.home = directory / "home"
        self.port = free_port()
        env = dict(
            os.environ,
            HOME=str(directory),
            HUSH_HOME=str(self.home),
            HUSH_CONFIG_DIR=str(directory / "config"),
            HUSH_PASS_HELPER=pass_helper,
            HUSH_AUTO_UPDATE="0",
        )
        if fake_dir is not None:
            env["HUSH_FAKE_PASS_DIR"] = str(fake_dir)
        env.pop("XDG_CONFIG_HOME", None)
        self.environment = env
        self.log = (directory / "relay.log").open("w+")
        self.process = None

    def start(self):
        self.process = subprocess.Popen(
            [os.environ.get("HUSH_TEST_RELAY_BIN", str(ROOT / "hush-relay")),
             "--no-open", str(self.port)],
            cwd=ROOT, env=self.environment,
            stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(100):
            try:
                self.call("GET", "/api/session")
                return
            except (OSError, ValueError):
                time.sleep(0.05)
        raise AssertionError("save_pass: relay did not start")

    def stop(self):
        if self.process is None:
            return
        try:
            self.call("POST", "/api/exit", {})
            self.process.wait(timeout=5)
        except (OSError, ValueError, subprocess.TimeoutExpired):
            self.process.terminate()
            self.process.wait(timeout=5)
        self.process = None

    def call(self, method, path, body=None):
        payload = body if body is None or isinstance(body, bytes) else json.dumps(
            body, separators=(",", ":"), ensure_ascii=False).encode()
        token = (self.home / "session.token").read_text().strip()
        for _ in range(40):
            request = urllib.request.Request(
                f"http://127.0.0.1:{self.port}{path}", data=payload,
                method=method,
                headers={"Content-Type": "application/json",
                         "X-Hush-Token": token})
            try:
                response = urllib.request.urlopen(request, timeout=5)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                raw = response.read()
                if response.status != 429:
                    return (response.status,
                            response.headers.get("Content-Type", ""), raw)
            time.sleep(0.25)
        raise AssertionError(f"save_pass: {path} stayed rate-limited")

    def ok(self, path, body):
        status, _, raw = self.call("POST", path, body)
        if status != 200:
            raise AssertionError(f"save_pass setup {path} -> {status} {raw[:200]!r}")
        return json.loads(raw)

    def session(self):
        status, _, raw = self.call("GET", "/api/session")
        if status != 200:
            raise AssertionError(f"save_pass session -> {status}")
        return json.loads(raw)


def robot(name, **extra):
    body = {"name": name, "system_prompt": "Walk the floor.",
            "provider": "grok-build", "save_pass": True}
    body.update(extra)
    return body


def boot(relay: Relay):
    relay.ok("/api/identity", {"action": "create"})
    relay.ok("/api/identity", {"action": "ack_backup", "save_pass": False})
    relay.ok("/api/vibe", {"name": "HQ", "about": "save-pass"})


def expect_400(relay, case, body, reason):
    status, ctype, raw = relay.call("POST", "/api/agent", body)
    want = (reason + "\n").encode()
    if status == 400 and raw == want and ctype.startswith("text/plain"):
        print(f"save_pass: ok {case}")
        return
    FAILURES.append(
        f"{case}: want HTTP 400 text/plain {want!r}; "
        f"got HTTP {status} {ctype} {raw[:200]!r}")


def check_missing():
    with tempfile.TemporaryDirectory(prefix="hush-sp-miss-") as raw:
        relay = Relay(Path(raw), "/nonexistent/pass")
        relay.start()
        try:
            boot(relay)
            before = [a.get("slug") for a in relay.session().get("agents", [])]
            expect_400(relay, "create save_pass missing pass",
                       robot("Delta"), PASS_MISSING)
            after = [a.get("slug") for a in relay.session().get("agents", [])]
            if after != before:
                FAILURES.append(f"missing create must not add a robot; {before} -> {after}")
            else:
                print("save_pass: ok missing create added no robot")
            err = relay.session().get("pass_error", "")
            if err != "pass is not available":
                FAILURES.append(f"missing must surface pass_error; got {err!r}")
            else:
                print("save_pass: ok missing pass_error in session")
        finally:
            relay.stop()


def check_fail():
    with tempfile.TemporaryDirectory(prefix="hush-sp-fail-") as raw:
        root = Path(raw)
        helper = root / "failpass.sh"
        helper.write_text("#!/bin/sh\nexit 1\n")
        helper.chmod(0o755)
        relay = Relay(root, str(helper))
        relay.start()
        try:
            boot(relay)
            before = [a.get("slug") for a in relay.session().get("agents", [])]
            expect_400(relay, "create save_pass helper fail",
                       robot("Charlie"), PASS_FAIL)
            after = [a.get("slug") for a in relay.session().get("agents", [])]
            if after != before:
                FAILURES.append(f"fail create must not add a robot; {before} -> {after}")
            else:
                print("save_pass: ok fail create added no robot")
            err = relay.session().get("pass_error", "")
            if "pass helper failed" not in err:
                FAILURES.append(f"fail must surface pass_error; got {err!r}")
            else:
                print("save_pass: ok fail pass_error in session")
        finally:
            relay.stop()


def check_update_honours():
    with tempfile.TemporaryDirectory(prefix="hush-sp-upd-") as raw:
        root = Path(raw)
        fake = root / "fakepass"
        fake.mkdir()
        helper = str(ROOT / "tests" / "fake-pass.sh")
        relay = Relay(root, helper, fake)
        relay.start()
        try:
            boot(relay)
            created = relay.ok("/api/agent", robot("Bravo", save_pass=False))
            slugs = [a.get("slug") for a in created.get("agents", [])]
            if "bravo" not in slugs:
                FAILURES.append(f"update setup missing bravo; {slugs}")
                return
            # No pass entry yet.
            store_before = sorted(p.name for p in fake.iterdir())
            status, _, raw = relay.call(
                "POST", "/api/agent",
                {"action": "update", "slug": "bravo", "system_prompt": "Updated.",
                 "save_pass": True})
            if status != 200:
                FAILURES.append(f"update save_pass:true want 200; got {status} {raw[:200]!r}")
                return
            store_after = sorted(p.name for p in fake.iterdir())
            if "agents_bravo_nsec" not in store_after:
                FAILURES.append(
                    f"update save_pass:true must store agents/bravo/nsec; "
                    f"before={store_before} after={store_after}")
            else:
                print("save_pass: ok update honour stored agents_bravo_nsec")
        finally:
            relay.stop()


def check_c_defines():
    text = (ROOT / "src" / "api_agents.c").read_text()
    for need in (PASS_MISSING, PASS_PATH, "Could not save the robot's key to pass: %s."):
        if need not in text:
            FAILURES.append(f"api_agents.c missing reason text: {need!r}")
    else:
        print("save_pass: ok C reason strings present")


def main():
    check_c_defines()
    check_missing()
    check_fail()
    check_update_honours()
    if FAILURES:
        for line in FAILURES:
            print(f"save_pass check failed: {line}", file=sys.stderr)
        sys.exit(1)
    print("save_pass checks ok")


if __name__ == "__main__":
    main()
