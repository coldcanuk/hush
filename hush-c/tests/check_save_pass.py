#!/usr/bin/env python3
"""Issue #235: create/update with save_pass must not report plain OK on pass fail.

Pins HTTP 400 reasons (missing|fail) and session robot_pass_error. N1: update
400 leaves roster unchanged. N2: save_pass off still offers templates to op.
B1/B2 context why; B3 identity isolation; B4a no op widen when save_pass on.
M7–M9 template pass write / update refuse. Path pinned in test_save_pass.c.
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
NO_FILES = "Ollama cannot read context files."

FAILURES: list[str] = []


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Relay:
    def __init__(self, directory: Path, pass_helper: str, fake_dir: Path | None = None,
                 extra_env: dict | None = None):
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
        if extra_env:
            env.update(extra_env)
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
            sess = relay.session()
            err = sess.get("robot_pass_error", "")
            if err != "pass is not available":
                FAILURES.append(f"missing must surface robot_pass_error; got {err!r}")
            else:
                print("save_pass: ok missing robot_pass_error in session")
            if sess.get("pass_error", ""):
                FAILURES.append(
                    f"missing must leave identity pass_error empty; got "
                    f"{sess.get('pass_error')!r}")
        finally:
            relay.stop()


def check_fail():
    with tempfile.TemporaryDirectory(prefix="hush-sp-fail-") as raw:
        root = Path(raw)
        helper = root / "failpass.sh"
        helper.write_text("#!/bin/sh\ncat >/dev/null\nexit 1\n")
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
            sess = relay.session()
            err = sess.get("robot_pass_error", "")
            if "pass helper failed" not in err:
                FAILURES.append(f"fail must surface robot_pass_error; got {err!r}")
            else:
                print("save_pass: ok fail robot_pass_error in session")
            if sess.get("pass_error", ""):
                FAILURES.append(
                    f"fail must leave identity pass_error empty; got "
                    f"{sess.get('pass_error')!r}")
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
            # B3d: successful update must not flip identity pass_saved.
            if relay.session().get("pass_saved") is True:
                FAILURES.append(
                    "B3d update success must leave identity pass_saved false")
            else:
                print("save_pass: ok B3d pass_saved stays false after update")
        finally:
            relay.stop()


def op_titles(fake_op: Path) -> set[str]:
    items = fake_op / "items" / "Hush"
    if not items.is_dir():
        return set()
    return {p.name for p in items.iterdir() if p.is_file()}


def check_template_pass_write():
    """M7: vibe create with save_pass writes template keys to pass only.
    B4a: with fake op armed, save_pass on must not widen to op."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-m7-") as raw:
        root = Path(raw)
        fake = root / "fakepass"
        fake.mkdir()
        fake_op = root / "fakeop"
        fake_op.mkdir()
        helper = str(ROOT / "tests" / "fake-pass.sh")
        op_helper = str(ROOT / "tests" / "fake-op.py")
        relay = Relay(root, helper, fake, extra_env={
            "HUSH_OP_HELPER": op_helper,
            "HUSH_FAKE_OP_DIR": str(fake_op),
        })
        relay.start()
        try:
            relay.ok("/api/identity", {"action": "create"})
            relay.ok("/api/identity", {"action": "ack_backup", "save_pass": True})
            relay.ok("/api/vibe", {"name": "HQ", "about": "save-pass"})
            store = sorted(p.name for p in fake.iterdir())
            for need in ("agents_coach_nsec", "agents_auditor_nsec",
                         "agents_marshal_nsec"):
                if need not in store:
                    FAILURES.append(
                        f"M7 template pass write missing {need}; store={store}")
            else:
                print("save_pass: ok M7 template keys in pass")
            titles = op_titles(fake_op)
            for need in ("hush-agents-coach-nsec", "hush-agents-auditor-nsec",
                         "hush-agents-marshal-nsec"):
                if need in titles:
                    FAILURES.append(
                        f"B4a save_pass on must not widen to op; saw {need} in {titles}")
            else:
                print("save_pass: ok B4a no op widen when save_pass on")
        finally:
            relay.stop()


def check_template_offer_when_save_pass_off():
    """N2: save_pass off restores base offer — templates land in op."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-n2-") as raw:
        root = Path(raw)
        fake_op = root / "fakeop"
        fake_op.mkdir()
        op_helper = str(ROOT / "tests" / "fake-op.py")
        # Pass helper override alone would hide PATH op; explicit OP_HELPER wins.
        relay = Relay(root, "/nonexistent/pass", extra_env={
            "HUSH_OP_HELPER": op_helper,
            "HUSH_FAKE_OP_DIR": str(fake_op),
        })
        relay.start()
        try:
            relay.ok("/api/identity", {"action": "create"})
            relay.ok("/api/identity", {"action": "ack_backup", "save_pass": False})
            relay.ok("/api/vibe", {"name": "HQ", "about": "save-pass"})
            titles = op_titles(fake_op)
            for need in ("hush-agents-coach-nsec", "hush-agents-auditor-nsec",
                         "hush-agents-marshal-nsec"):
                if need not in titles:
                    FAILURES.append(
                        f"N2 save_pass off must offer templates to op; "
                        f"missing {need}; have {titles}")
            else:
                print("save_pass: ok N2 templates offered to op when save_pass off")
        finally:
            relay.stop()


def check_update_refuses_fail():
    """M8/M9/N1: update save_pass:true with failing helper → 400, prompt unchanged."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-m8-") as raw:
        root = Path(raw)
        helper = root / "failpass.sh"
        helper.write_text("#!/bin/sh\ncat >/dev/null\nexit 1\n")
        helper.chmod(0o755)
        relay = Relay(root, str(helper))
        relay.start()
        try:
            boot(relay)
            created = relay.ok("/api/agent", robot("Echo", save_pass=False))
            agents = {a.get("slug"): a for a in created.get("agents", [])}
            before = agents.get("echo", {}).get("system_prompt") or agents.get("echo", {}).get("prompt")
            # Session agents may use 'prompt' field — probe both.
            sess = relay.session()
            echo = next((a for a in sess.get("agents", []) if a.get("slug") == "echo"), None)
            if echo is None:
                FAILURES.append("M8 setup missing echo")
                return
            prompt_before = echo.get("system_prompt") or echo.get("prompt") or ""
            expect_400(
                relay, "update save_pass helper fail",
                {"action": "update", "slug": "echo", "system_prompt": "Changed.",
                 "save_pass": True},
                PASS_FAIL)
            sess = relay.session()
            echo = next((a for a in sess.get("agents", []) if a.get("slug") == "echo"), None)
            prompt_after = (echo or {}).get("system_prompt") or (echo or {}).get("prompt") or ""
            if prompt_after != prompt_before:
                FAILURES.append(
                    f"N1 update 400 must leave prompt unchanged; "
                    f"before={prompt_before!r} after={prompt_after!r}")
            else:
                print("save_pass: ok N1 prompt unchanged after update 400")
            err = sess.get("robot_pass_error", "")
            if "pass helper failed" not in err and err != "save failed":
                FAILURES.append(f"M9 update must set robot_pass_error; got {err!r}")
            else:
                print("save_pass: ok M9 update robot_pass_error")
            if sess.get("pass_error", ""):
                FAILURES.append(
                    f"M9 must leave identity pass_error empty; got "
                    f"{sess.get('pass_error')!r}")
            if sess.get("pass_saved") is True:
                FAILURES.append("B3c update fail must not set pass_saved true")
        finally:
            relay.stop()


def check_sticky_save_pass_false():
    """B2: sticky robot_pass_error must not replace a refused save_pass:false why."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-b2-") as raw:
        root = Path(raw)
        fake = root / "fakepass"
        fake.mkdir()
        helper = str(ROOT / "tests" / "fake-pass.sh")
        relay = Relay(root, "/nonexistent/pass")
        relay.start()
        try:
            boot(relay)
            expect_400(relay, "sticky setup missing", robot("Delta"), PASS_MISSING)
            # Refused save_pass:false with Ollama+context must keep context why.
            status, ctype, raw = relay.call(
                "POST", "/api/agent",
                robot("Foxtrot", save_pass=False, provider="ollama",
                      context_name_0="brief.md",
                      context_mime_0="text/markdown",
                      context_text_0="hi"))
            want = (NO_FILES + "\n").encode()
            if status == 400 and raw == want and ctype.startswith("text/plain"):
                print("save_pass: ok B2 sticky does not steal context why")
            else:
                FAILURES.append(
                    f"B2 save_pass:false context refuse want {want!r}; "
                    f"got HTTP {status} {ctype} {raw[:200]!r}")
        finally:
            relay.stop()


def check_context_denied_with_save_pass():
    """B1a/B1b: save_pass:true + Ollama context keeps context why, not pass."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-b1-") as raw:
        root = Path(raw)
        fake = root / "fakepass"
        fake.mkdir()
        helper = str(ROOT / "tests" / "fake-pass.sh")
        relay = Relay(root, helper, fake)
        relay.start()
        try:
            boot(relay)
            expect_400(
                relay, "B1 save_pass + ollama context",
                robot("Golf", save_pass=True, provider="ollama",
                      context_name_0="brief.md",
                      context_mime_0="text/markdown",
                      context_text_0="hi"),
                NO_FILES)
            sess = relay.session()
            if sess.get("robot_pass_error", ""):
                FAILURES.append(
                    f"B1 must leave robot_pass_error empty; got "
                    f"{sess.get('robot_pass_error')!r}")
            else:
                print("save_pass: ok B1 context why leaves robot_pass_error empty")
            if sess.get("pass_error", ""):
                FAILURES.append(
                    f"B1 must leave identity pass_error empty; got "
                    f"{sess.get('pass_error')!r}")
        finally:
            relay.stop()


def check_robot_success_pass_saved():
    """B3b: successful robot save_pass must not set identity pass_saved."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-b3b-") as raw:
        root = Path(raw)
        fake = root / "fakepass"
        fake.mkdir()
        helper = str(ROOT / "tests" / "fake-pass.sh")
        relay = Relay(root, helper, fake)
        relay.start()
        try:
            boot(relay)
            if relay.session().get("pass_saved") is True:
                FAILURES.append("B3b setup: pass_saved already true")
                return
            relay.ok("/api/agent", robot("Hotel", save_pass=True))
            sess = relay.session()
            if sess.get("pass_saved") is True:
                FAILURES.append("B3b robot success must leave pass_saved false")
            else:
                print("save_pass: ok B3b pass_saved stays false after robot save")
        finally:
            relay.stop()



def check_seed_never_blocks_vibe():
    """M6: save_pass true + missing pass must still create vibe (KEY_NONE seed)."""
    with tempfile.TemporaryDirectory(prefix="hush-sp-m6-") as raw:
        root = Path(raw)
        relay = Relay(root, "/nonexistent/pass")
        relay.start()
        try:
            relay.ok("/api/identity", {"action": "create"})
            relay.ok("/api/identity", {"action": "ack_backup", "save_pass": True})
            status, _, rawb = relay.call("POST", "/api/vibe",
                                         {"name": "HQ", "about": "save-pass"})
            if status != 200:
                FAILURES.append(
                    f"M6 vibe with save_pass+missing pass want 200; "
                    f"got {status} {rawb[:200]!r}")
            else:
                print("save_pass: ok M6 vibe not blocked when pass missing")
            sess = relay.session()
            slugs = {a.get("slug") for a in sess.get("agents", [])}
            for need in ("coach", "auditor", "marshal"):
                if need not in slugs:
                    FAILURES.append(f"M6 templates missing {need}; {slugs}")
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
    check_template_pass_write()
    check_template_offer_when_save_pass_off()
    check_update_refuses_fail()
    check_sticky_save_pass_false()
    check_context_denied_with_save_pass()
    check_robot_success_pass_saved()
    check_seed_never_blocks_vibe()
    if FAILURES:
        for line in FAILURES:
            print(f"save_pass check failed: {line}", file=sys.stderr)
        sys.exit(1)
    print("save_pass checks ok")


if __name__ == "__main__":
    main()
