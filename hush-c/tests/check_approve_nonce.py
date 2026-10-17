#!/usr/bin/env python3
"""Approvals need the paired window, not the token (#336 part 5, Refs #322).

With Approve every action on, a robot turn is held until the owner answers
Yes. That answer, and any approval_mode write, needs the X-Hush-Nonce the
relay mints in memory and hands out only on the one-time pairing link.
session.token alone (header, Bearer, or the hush_session cookie it is) is
refused 403. A stored approval_mode the relay does not know asks every
action (strictest); a vibe that never saved one keeps auto-approve.

The robot is a stub grok on a stub PATH that counts its own starts; the
attacker is a separate stub process that reads session.token from disk.
Other provider CLIs on PATH are canaries.
"""

import json
import os
import re
import secrets
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__ in the tree
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_bootstrap import Relay, head_of, sign_in, status_of  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
EVERY = "approve_every_action"
AUTO = "auto_approve"
ASK = "Approval needed: Happy wants to take a turn. Reply Yes or No in this thread."

# The attacker: a local process with the token from disk and no X-Hush-Nonce.
ATTACKER = r'''
import json, socket, sys
port, token, root = int(sys.argv[1]), sys.argv[2], sys.argv[3]
def post(path, body, cred):
    data = json.dumps(body).encode()
    head = (f"POST {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n{cred}\r\n"
            f"Content-Type: application/json\r\nContent-Length: {len(data)}\r\n"
            "Connection: close\r\n\r\n").encode()
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(head + data)
    raw = b""
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        raw += chunk
    s.close()
    return int(raw.split(b" ")[1])
creds = [f"X-Hush-Token: {token}", f"Authorization: Bearer {token}",
         f"Cookie: hush_session={token}",
         f"X-Hush-Token: {token}" + chr(13) + chr(10) + f"X-Hush-Nonce: {token}"]
out = []
for cred in creds:
    for text in ("Yes", "yes.", " YES! ", "No"):
        note = {"content": text, "kind": 1, "channel": "general", "reply_to": root}
        out.append(["answer " + repr(text) + " via " + cred.split(":")[0], post("/api/event", note, cred)])
    out.append(["approval_mode via " + cred.split(":")[0],
                post("/api/profile", {"approval_mode": "auto_approve"}, cred)])
print(json.dumps(out))
'''


def write_grok(bin_dir, count_file):
    grok = bin_dir / "grok"
    grok.write_text("#!/bin/sh\n"
                    f'n=$(cat "{count_file}" 2>/dev/null || echo 0)\n'
                    f'echo $((n + 1)) > "{count_file}"\n'
                    'echo "lead turn done."\n')
    grok.chmod(0o755)


class Hive:
    def __init__(self, directory):
        self.relay = Relay(directory)
        # Count file lives under bin/: Landlock grants that tree, but not
        # siblings of the denied $HUSH_HOME / config dirs (#336 part 1).
        self.count = directory / "bin" / "grok.count"
        write_grok(directory / "bin", self.count)
        (directory / "user" / ".grok").mkdir(parents=True)
        (directory / "user" / ".grok" / "auth.json").write_text('{"ok":true}\n')
        (directory / "fakepass").mkdir()
        self.relay.environment.update(
            HUSH_PASS_HELPER=str(ROOT / "tests" / "fake-pass.sh"),
            HUSH_FAKE_PASS_DIR=str(directory / "fakepass"))
        self.ui = None

    def token_header(self):
        return [f"X-Hush-Token: {self.relay.token()}"]

    def paired(self):
        # A browser POST from the served page also carries its own Origin (#338).
        return [f"Cookie: hush_session={self.relay.token()}", f"X-Hush-Nonce: {self.ui}",
                f"Origin: http://{self.relay.host}", "Sec-Fetch-Site: same-origin"]

    def pair(self):
        raw = self.relay.send("GET", "/?pair=" + self.relay.pair_code(), [])
        found = re.search(rb"(?i)X-Hush-Nonce: ([0-9a-f]{32})", head_of(raw))
        assert found, head_of(raw)[:400]
        self.ui = found.group(1).decode()
        assert self.ui != self.relay.token(), "X-Hush-Nonce must not be the owner token"
        # A session cookie (no Max-Age / Expires): the browser never writes it to disk.
        want = f"Set-Cookie: hush_nonce={self.ui}; SameSite=Strict; Path=/\r\n"
        assert want.encode() in head_of(raw) + b"\r\n", head_of(raw)[:400]

    def post(self, path, body, headers):
        raw = self.relay.send("POST", path, headers, body)
        return status_of(raw), raw.split(b"\r\n\r\n", 1)[1]

    def mode(self):
        raw = self.relay.send("GET", "/api/session", self.token_header())
        return json.loads(raw.split(b"\r\n\r\n", 1)[1])["approval_mode"]

    def starts(self):
        return int(self.count.read_text()) if self.count.exists() else 0

    def events(self):
        raw = self.relay.send("GET", "/api/events", self.token_header())
        return json.loads(raw.split(b"\r\n\r\n", 1)[1])["events"]


def raise_happy(hive):
    sign_in(hive.relay)
    st, _ = hive.post("/api/vibe", {"name": "HQ", "about": "nonce"}, hive.token_header())
    assert st == 200
    st, body = hive.post("/api/agent", {"name": "Happy", "system_prompt": "Play.",
                                        "provider": "grok-build", "save_pass": True},
                         hive.token_header())
    assert st == 200, body[:200]
    agents = json.loads(body).get("agents") or []
    npub = next(a["npub"] for a in agents if a.get("slug") == "happy")
    return npub


def held_root(hive, npub):
    note = {"content": f"nostr:{npub} tell a joke tag-n1", "kind": 1, "channel": "general",
            "mention_0": npub}
    st, body = hive.post("/api/event", note, hive.paired())
    assert st == 200, ("the paired window posts the owner's note", body[:200])
    for _ in range(200):
        events = hive.events()
        roots = [e["id"] for e in events if "tag-n1" in (e.get("content") or "")
                 and not e.get("reply_to")]
        if roots and any(e.get("content") == ASK and e.get("reply_to") == roots[0]
                         for e in events):
            return roots[0]
        time.sleep(0.05)
    raise AssertionError("the turn was never held for approval")


def check_mode_gate(hive):
    st, _ = hive.post("/api/profile", {"approval_mode": EVERY}, hive.token_header())
    assert st == 403 and hive.mode() == AUTO, "approval_mode is not a token action"
    hive.pair()
    st, _ = hive.post("/api/profile", {"approval_mode": EVERY}, hive.paired())
    assert st == 200 and hive.mode() == EVERY, "the paired window sets approval_mode"
    st, _ = hive.post("/api/profile", {"approval_mode": "maybe"}, hive.paired())
    assert st == 400 and hive.mode() == EVERY, "an unknown mode is refused"
    cookie = f"Cookie: hush_session={hive.relay.token()}"
    for bad in (hive.ui[:-1], hive.ui[:1], hive.ui + "0", secrets.token_hex(16)):
        st, _ = hive.post("/api/profile", {"approval_mode": AUTO},
                          [cookie, f"X-Hush-Nonce: {bad}"])
        assert st == 403 and hive.mode() == EVERY, ("near-miss X-Hush-Nonce", bad)
    print("approve nonce: approval_mode token-only 403, paired 200, unknown 400, "
          "short/one-char/long/fresh X-Hush-Nonce 403 OK")


def check_attacker(hive, root):
    env = {"PATH": hive.relay.environment["PATH"]}
    token = (hive.relay.directory / "home" / "session.token").read_text().strip()
    done = subprocess.run([sys.executable, "-c", ATTACKER, str(hive.relay.port), token, root],
                          env=env, capture_output=True, text=True, timeout=60, check=True)
    for label, status in json.loads(done.stdout):
        assert status == 403, (label, status)
    time.sleep(1.5)
    assert hive.starts() == 0, "a token-only Yes ran the held turn"
    assert hive.mode() == EVERY, "a token-only write turned approvals off"
    print("approve nonce: stub attacker with session.token: every Yes/No and mode write 403 OK")


def check_paired_yes(hive, root):
    note = {"content": "Yes", "kind": 1, "channel": "general", "reply_to": root}
    st, body = hive.post("/api/event", note, hive.paired())
    assert st == 200, body[:200]
    for _ in range(200):
        if hive.starts() == 1:
            break
        time.sleep(0.05)
    time.sleep(1.0)
    assert hive.starts() == 1, f"the paired Yes must run the turn once ({hive.starts()})"
    print("approve nonce: paired-window Yes runs the held stub once OK")


def check_stored_mode(hive):
    vibe = hive.relay.directory / "config" / "vibe.json"
    for stored, want in (('"bogus"', EVERY), ('""', EVERY), ("7", EVERY)):
        hive.relay.stop()
        text = re.sub(r'"approval_mode":("[^"]*"|\d+)', '"approval_mode":' + stored,
                      vibe.read_text())
        assert '"approval_mode":' + stored in text, text[:200]
        vibe.write_text(text)
        hive.relay.start()
        assert hive.mode() == want, (stored, hive.mode())
    hive.relay.stop()
    vibe.write_text(re.sub(r',?"approval_mode":\d+', "", vibe.read_text()))
    assert '"approval_mode"' not in vibe.read_text()
    hive.relay.start()
    assert hive.mode() == AUTO, "a vibe that never saved the key keeps the default"
    raw = hive.relay.send("GET", "/", [f"X-Hush-Nonce: {hive.ui}"])
    st, _ = hive.post("/api/profile", {"approval_mode": EVERY}, hive.paired())
    assert st == 403, "a restart ends the old X-Hush-Nonce"
    print("approve nonce: stored unknown/empty/non-string mode asks every action; "
          "missing keeps auto; restart ends X-Hush-Nonce OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-nonce-") as temporary:
        hive = Hive(Path(temporary))
        hive.relay.start()
        try:
            npub = raise_happy(hive)
            check_mode_gate(hive)
            root = held_root(hive, npub)
            assert hive.starts() == 0
            check_attacker(hive, root)
            check_paired_yes(hive, root)
            check_stored_mode(hive)
        finally:
            hive.relay.stop()
        hits = hive.relay.hits
        assert not hits.exists(), hits.read_text()
    print("approve nonce: provider canaries silent OK")


if __name__ == "__main__":
    main()
