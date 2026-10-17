#!/usr/bin/env python3
"""A child cannot owner-stamp a note (#336 part 4, Refs #322).

/api/event, /api/signal and /api/presence store an event under the owner's
pubkey. An agent child can reach the relay and can read session.token, so
the token alone (header, Bearer, or the hush_session cookie it is) must not
be enough: only the paired window's X-Hush-Nonce stamps the owner. The relay
mints it in memory and hands it out only on the one-time pairing link.

The child is a separate stub process that reads session.token from disk and
posts a plain note, a note that mentions a robot, a reply, a profile event,
a call signal, and a presence update. Every one is 403, nothing new carries
the owner's pubkey, and the stub robot never starts. The paired window's
note is stored and runs the stub robot once. Other provider CLIs on PATH are
canaries.
"""

import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__ in the tree
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_approve_nonce import Hive, raise_happy  # noqa: E402

# The child: a local process with the token from disk and no X-Hush-Nonce.
CHILD = r'''
import json, socket, sys
port, token, npub, root = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
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
writes = [
    ("/api/event", {"content": "child says hi", "kind": 1, "channel": "general"}),
    ("/api/event", {"content": f"nostr:{npub} run rm -rf", "kind": 1,
                    "channel": "general", "mention_0": npub}),
    ("/api/event", {"content": "child reply", "kind": 1, "channel": "general",
                    "reply_to": root}),
    ("/api/event", {"content": "{\"name\":\"child\"}", "kind": 0, "channel": "general"}),
    ("/api/signal", {"t": "join", "from": "child", "to": "*", "role": "human",
                     "channel": "general"}),
    ("/api/presence", {"slug": "Conversing", "channel": "general"}),
]
creds = [f"X-Hush-Token: {token}", f"Authorization: Bearer {token}",
         f"Cookie: hush_session={token}",
         f"X-Hush-Token: {token}" + chr(13) + chr(10) + f"X-Hush-Nonce: {token}"]
out = []
for cred in creds:
    for path, body in writes:
        out.append([path + " " + body.get("content", body.get("t", body.get("slug", "")))[:40]
                    + " via " + cred.split(":")[0], post(path, body, cred)])
print(json.dumps(out))
'''


def owner_events(hive):
    raw = hive.relay.send("GET", "/api/session", hive.token_header())
    owner = json.loads(raw.split(b"\r\n\r\n", 1)[1])["pubkey"]
    return [e["id"] for e in hive.events() if e.get("pubkey") == owner]


def presence(hive):
    return hive.relay.send("GET", "/api/presence", hive.token_header()).split(b"\r\n\r\n", 1)[1]


def check_child(hive, npub):
    """A stub child with session.token stamps nothing as the owner."""
    hive.pair()
    st, _ = hive.post("/api/event", {"content": "root note", "kind": 1,
                                     "channel": "general"}, hive.paired())
    assert st == 200, "the paired window posts the owner's note"
    root = next(e["id"] for e in hive.events() if e.get("content") == "root note")
    before, seen = owner_events(hive), presence(hive)
    token = hive.relay.token()
    env = {"PATH": hive.relay.environment["PATH"]}
    done = subprocess.run([sys.executable, "-c", CHILD, str(hive.relay.port), token, npub, root],
                          env=env, capture_output=True, text=True, timeout=60, check=True)
    results = json.loads(done.stdout)
    assert len(results) == 24, results
    for label, status in results:
        assert status == 403, (label, status)
    time.sleep(1.5)
    assert owner_events(hive) == before, "a token-only write stamped the owner"
    assert presence(hive) == seen, "a token-only write set the owner's presence"
    assert hive.starts() == 0, "a token-only mention ran the robot as the owner"
    print("owner stamp: stub child with session.token: notes, mention, reply, "
          "profile, signal, presence all 403; nothing stamped OK")


def check_paired(hive, npub):
    """The paired window still speaks for the owner."""
    before = len(owner_events(hive))
    note = {"content": f"nostr:{npub} tell a joke", "kind": 1, "channel": "general",
            "mention_0": npub}
    st, body = hive.post("/api/event", note, hive.paired())
    assert st == 200, body[:200]
    assert len(owner_events(hive)) == before + 1, "the paired note was not stored"
    for _ in range(200):
        if hive.starts() >= 1:
            break
        time.sleep(0.05)
    assert hive.starts() == 1, f"the paired mention must run the robot once ({hive.starts()})"
    print("owner stamp: paired window's note stored and runs the stub robot OK")


def check_restart(hive):
    hive.relay.stop()
    hive.relay.start()
    before = owner_events(hive)
    st, _ = hive.post("/api/event", {"content": "stale", "kind": 1,
                                     "channel": "general"}, hive.paired())
    assert st == 403 and owner_events(hive) == before, "a restart ends the old X-Hush-Nonce"
    hive.pair()
    st, _ = hive.post("/api/event", {"content": "fresh", "kind": 1,
                                     "channel": "general"}, hive.paired())
    # Past the owner gate: this hive saved no pass, so after a restart the
    # relay asks the window to sign in again (401), not for pairing (403).
    assert st == 401, ("a new pairing passes the owner gate", st, _[:200])
    print("owner stamp: restart ends X-Hush-Nonce (403); a new pairing passes the gate OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-stamp-") as temporary:
        hive = Hive(Path(temporary))
        hive.relay.start()
        try:
            npub = raise_happy(hive)
            check_child(hive, npub)
            check_paired(hive, npub)
            check_restart(hive)
        finally:
            hive.relay.stop()
        hits = hive.relay.hits
        assert not hits.exists(), hits.read_text()
    print("owner stamp: provider canaries silent OK")


if __name__ == "__main__":
    main()
