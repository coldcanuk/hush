#!/usr/bin/env python3
"""Real relay routes with isolated storage and explicit test harness fixtures."""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class Relay:
    def __init__(self, directory):
        self.directory = directory
        self.port = free_port()
        self.environment = dict(os.environ, HUSH_HOME=str(directory / "home"),
                                HUSH_CONFIG_DIR=str(directory / "config"),
                                PASSWORD_STORE_DIR=str(directory / "pass"),
                                HUSH_AUTO_UPDATE="0")
        self.log = (directory / "relay.log").open("w+")
        self.process = None

    def start(self):
        self.process = subprocess.Popen([os.environ.get("HUSH_TEST_RELAY_BIN", str(ROOT / "hush-relay")), "--no-open", str(self.port)],
                                        cwd=ROOT, env=self.environment, stdout=self.log,
                                        stderr=subprocess.STDOUT)
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
        request = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=payload,
                                         headers={"Content-Type": "application/json",
                                                  "X-Hush-Token": token})
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            assert response.status == expected, (path, response.status, expected, raw[:160])
            return json.loads(raw) if expected == 200 else raw


def channel(session, slug):
    return next(room for room in session["channels"] if room["slug"] == slug)


def check_rooms(relay):
    relay.request("/api/event", {"content": "Unowned message"}, expected=401)
    relay.request("/api/identity", {"action": "create"})
    relay.request("/api/identity", {"action": "ack_backup", "save_pass": False})
    session = relay.request("/api/vibe", {"name": "Collaboration test", "about": "Temporary"})
    assert all(0 < len(room["system_prompt"]) <= 500 for room in session["channels"])
    relay.request("/api/channel", {"action": "manage", "slug": "general", "robot_0": "sgt-major-payne"})
    session = relay.request("/api/channel", {"action": "manage", "slug": "general", "system_prompt": "Keep the room focused."})
    assert channel(session, "general")["robots"] == ["sgt-major-payne"]
    session = relay.request("/api/channel", {"action": "manage", "slug": "general", "robot_0": ""})
    assert channel(session, "general")["robots"] == []
    prompt = "Help us finish the research."
    session = relay.request("/api/channel", {"name": "Research", "system_prompt": prompt})
    assert channel(session, "research")["system_prompt"] == prompt
    for text in ("x" * 500, "🌿" * 500, "é" * 500, '\n"\\' * 166 + "ok"):
        session = relay.request("/api/channel", {"action": "manage", "slug": "research",
                                                 "system_prompt": text})
        assert channel(session, "research")["system_prompt"] == text.strip()
    before = channel(session, "research")
    for bad in ("x" * 501, "🌿" * 501, "prefix\x00hidden", "\ud800", 42, None):
        relay.request("/api/channel", {"action": "manage", "slug": "research",
                                        "system_prompt": bad}, expected=400)
        assert channel(relay.request("/api/session"), "research") == before
    relay.request("/api/channel", {"name": "Invalid", "system_prompt": "z" * 501}, expected=400)
    assert not any(room["slug"] == "invalid" for room in relay.request("/api/session")["channels"])
    relay.stop()
    relay.start()
    assert channel(relay.request("/api/session"), "research") == before
    print("room prompts: defaults, boundaries, Unicode, rejection, atomic validation, restart OK")


class Endpoint(BaseHTTPRequestHandler):
    requests = []
    mode = "ok"

    def log_message(self, *_args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.requests.append((self.path, body, dict(self.headers)))
        if self.path.endswith("/messages"):
            response = {"content": [{"type": "text", "text": "API_"},
                                    {"type": "text", "text": "REPLY_ANTHROPIC"}]}
        elif self.path.endswith(":generateContent"):
            response = {"candidates": [{"content": {"parts": [{"thought": True, "text": "PRIVATE_REASONING"}, {"text": "API_REPLY_GEMINI"}]}}]}
        else:
            text = "API_REPLY_" + body["model"]
            instructions = body["messages"][0]["content"]
            if "You are the election committee" in instructions:
                text = "Planner"
            elif "You are the leader. Organize" in instructions:
                text = "```plan\norder: fifo\n1 Writer: Draft the response.\n2 Reviewer: Review the draft.\n```"
            response = {"choices": [{"message": {"content": text}}]}
        if self.mode == "empty": response = {"choices": [{"message": {"content": ""}}]}
        if self.mode == "malformed": response = {"choices": [], "content": "FAKE_REPLY"}
        if self.mode in ("full", "oversized"):
            response = {"choices": [{"message": {"content": "B" * (4096 if self.mode == "full" else 4097)}}]}
        encoded = json.dumps(response).encode()
        self.send_response(503 if self.mode == "failure" else 200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def wait_reply(relay, marker):
    for _ in range(150):
        events = relay.request("/api/events")["events"]
        hits = [event for event in events if marker in event["content"]]
        if hits:
            return hits[-1]
        time.sleep(0.05)
    raise AssertionError("Expected harness reply missing: " + marker)


def check_providers(relay):
    relay.stop()
    binaries = relay.directory / "bin"
    binaries.mkdir()
    # Only this fixture stores test secrets in plaintext, inside a temporary directory.
    fake_pass = binaries / "pass"
    fake_pass.write_text("""#!/usr/bin/env python3
import os, sys
from pathlib import Path
path = Path(os.environ['PASSWORD_STORE_DIR']) / sys.argv[-1]
if sys.argv[1] == 'insert':
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(sys.stdin.read())
elif path.is_file():
    print(path.read_text().strip())
else:
    sys.exit(1)
""")
    fake_pass.chmod(0o755)
    relay.environment["PATH"] = str(binaries) + os.pathsep + os.environ["PATH"]
    relay.start()
    relay.request("/api/identity", {"action": "create"})
    relay.request("/api/identity", {"action": "ack_backup", "save_pass": False})
    with ThreadingHTTPServer(("127.0.0.1", 0), Endpoint) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host = "http://127.0.0.1:" + str(server.server_port)
        providers = ["openai-api", "xai-api", "deepseek-api", "anthropic-api", "gemini-api", "custom"]
        skill = relay.request("/api/skill", {"name": "Precision", "summary": "Preserve details.",
                              "body": "EQUIPPED_SKILL_PROOF: preserve all test details.", "scope": "user"})
        catalog = relay.request("/api/skills")
        skill_id = next(item["id"] for item in catalog["skills"] if item["id"].endswith(":precision"))
        for provider in providers:
            model = provider.replace("-", "_")
            relay.request("/api/provider", {"provider": provider, "host": host,
                                            "model": model, "api_key": "test-secret-only"})
            session = relay.request("/api/agent", {"name": provider, "system_prompt": "ROBOT_PROMPT_PROOF",
                         "provider": provider, "save_pass": True, "skill_0": skill_id})
            bot = next(agent for agent in session["agents"] if agent["name"] == provider)
            relay.request("/api/channel", {"action": "manage", "slug": "research",
                                            "system_prompt": "ROOM_PROMPT_PROOF"})
            relay.request("/api/event", {"kind": 1, "channel": "research", "mention_0": bot["npub"],
                                         "content": "nostr:" + bot["npub"] + " HUMAN_REQUEST_PROOF"})
            marker = "API_REPLY_" + model
            if provider == "anthropic-api": marker = "API_REPLY_ANTHROPIC"
            if provider == "gemini-api": marker = "API_REPLY_GEMINI"
            reply = wait_reply(relay, marker)
            assert reply["pubkey"] == bot["pubkey"] and reply["channel"] == "research"
            path, body, headers = Endpoint.requests[-1]
            serialized = json.dumps(body)
            assert "ROBOT_PROMPT_PROOF" in serialized and "ROOM_PROMPT_PROOF" in serialized
            assert "EQUIPPED_SKILL_PROOF" in serialized and "HUMAN_REQUEST_PROOF" in serialized
            assert "test-secret-only" not in serialized
            normalized = {name.lower(): value for name, value in headers.items()}
            secret_header = {"anthropic-api": "x-api-key", "gemini-api": "x-goog-api-key"}.get(provider, "authorization")
            assert normalized[secret_header] == ("Bearer test-secret-only" if secret_header == "authorization" else "test-secret-only")
            assert "PRIVATE_REASONING" not in reply["content"]
            if provider == "anthropic-api":
                assert path == "/v1/messages" and body["max_tokens"] > 0
            elif provider == "gemini-api":
                assert path == "/v1beta/models/gemini_api:generateContent"
            else:
                assert path == "/v1/chat/completions" and body["model"] == model
        check_memory(relay, bot, host, skill_id)
        Endpoint.mode = "full"
        boundary = post_thread(relay, bot, "MAXIMUM_RESPONSE_CHECK")
        wait_idle(relay)
        assert any(event.get("reply_to") == boundary["id"] and event["content"] == "B" * 4096
                   for event in relay.request("/api/events")["events"])
        Endpoint.mode = "ok"
        check_failures(relay, bot)
        check_chaining(relay)
        check_cline(relay, host)
        server.shutdown()
        thread.join(timeout=5)
    print("providers: six API routes, model selection, identity, room and equipped skill instructions OK")


def post_thread(relay, bot, content, root=None, room="research"):
    body = {"kind": 1, "channel": room, "content": content}
    if root:
        body["reply_to"] = root
    else:
        body["mention_0"] = bot["npub"]
    relay.request("/api/event", body)
    return next(event for event in reversed(relay.request("/api/events")["events"])
                if event["content"] == content)


def wait_request_count(expected):
    for _ in range(200):
        if len(Endpoint.requests) >= expected:
            return
        time.sleep(0.05)
    raise AssertionError("Provider request never arrived")


def wait_idle(relay):
    for _ in range(200):
        if not relay.request("/api/status")["thinking"]:
            return
        time.sleep(0.05)
    raise AssertionError("Agent did not finish")


def check_memory(relay, bot, host, skill_id):
    wait_idle(relay)
    root = post_thread(relay, bot, "Remember OLIVE_MEMORY_ROOT: " + "🌿" * 160)["id"]
    wait_idle(relay)
    # Unrelated traffic exceeds the old 64-event window and HTTP buffer.
    for index in range(100):
        relay.request("/api/event", {"channel": "general", "content":
                      f"UNRELATED_ROOM_{index}: " + "history buffer check. " * 75})
    before = len(Endpoint.requests)
    followup = "LATEST_HUMAN_REQUEST " + "detail " * 150 + " PRESERVE_THE_END"
    post_thread(relay, bot, followup, root)
    wait_request_count(before + 1)
    wait_idle(relay)
    payload = json.dumps(Endpoint.requests[-1][1])
    assert "OLIVE_MEMORY_ROOT" in payload and "PRESERVE_THE_END" in payload
    assert "UNRELATED_ROOM_" not in payload
    replies = [event for event in relay.request("/api/events")["events"] if event.get("reply_to") == root]
    assert any(event["pubkey"] == bot["pubkey"] and "API_REPLY" in event["content"] for event in replies)
    relay.request("/api/event", {"channel": "general", "content": "wrong room", "reply_to": root}, expected=400)
    relay.request("/api/event", {"channel": "research", "content": "missing root", "reply_to": "0" * 64}, expected=400)
    for turn in range(5):
        before = len(Endpoint.requests)
        post_thread(relay, bot, f"ONGOING_CONVERSATION_{turn}", root)
        wait_request_count(before + 1)
        wait_idle(relay)
    assert "OLIVE_MEMORY_ROOT" in json.dumps(Endpoint.requests[-1][1])
    relay.request("/api/agent", {"action": "update", "slug": bot["slug"], "skill_0": "", "nskills": 0})
    before = len(Endpoint.requests)
    post_thread(relay, bot, "REMOVED_SKILL_REQUEST", root)
    wait_request_count(before + 1)
    wait_idle(relay)
    assert "EQUIPPED_SKILL_PROOF" not in json.dumps(Endpoint.requests[-1][1])
    relay.request("/api/agent", {"action": "update", "slug": bot["slug"], "skill_0": skill_id,
                  "providers": "custom,openai-api", "provider": "custom"})
    saved = relay.request("/api/events")["events"]
    relay.stop()
    relay.start()
    restored = relay.request("/api/events")["events"]
    assert {event["id"] for event in saved} <= {event["id"] for event in restored}
    restored_bot = next(item for item in relay.request("/api/session")["agents"] if item["slug"] == bot["slug"])
    assert restored_bot["skills"] == [skill_id] and restored_bot["providers"] == ["custom", "openai-api"]
    # Restore a session after restart; the original creator remains on the stored root.
    relay.request("/api/identity", {"action": "create"})
    relay.request("/api/identity", {"action": "ack_backup", "save_pass": False})
    before = len(Endpoint.requests)
    post_thread(relay, bot, "AFTER_RESTART_REQUEST", root)
    wait_request_count(before + 1)
    wait_idle(relay)
    assert "OLIVE_MEMORY_ROOT" in json.dumps(Endpoint.requests[-1][1])
    assert Endpoint.requests[-1][1]["model"] == "custom"
    print("memory: >64 events, full current ask, room isolation, plain follow-up, skill removal, restart and order OK")


def check_failures(relay, bot):
    for mode in ("failure", "empty", "malformed"):
        Endpoint.mode = mode
        request = post_thread(relay, bot, "FAILURE_CASE_" + mode)
        wait_idle(relay)
        replies = [event for event in relay.request("/api/events")["events"]
                   if event.get("reply_to") == request["id"]]
        assert any("did not return a usable reply" in event["content"] for event in replies), replies
        assert not any("API_REPLY" in event["content"] or "FAKE_REPLY" in event["content"] for event in replies)
    Endpoint.mode = "oversized"
    request = post_thread(relay, bot, "FAILURE_CASE_oversized")
    wait_idle(relay)
    replies = [event for event in relay.request("/api/events")["events"]
               if event.get("reply_to") == request["id"]]
    assert any(event["content"] == "B" * 4096 for event in replies), replies
    assert not any("did not return a usable reply" in event["content"] for event in replies)
    Endpoint.mode = "ok"
    print("failures: HTTP error, empty output and malformed schema produce honest notices; oversized reply truncates OK")


def check_chaining(relay):
    bots = []
    for name in ("Planner", "Writer", "Reviewer"):
        session = relay.request("/api/agent", {"name": name, "provider": "custom",
                    "system_prompt": "TEAM_ROBOT_PROMPT " + name, "save_pass": True})
        bots.append(next(bot for bot in session["agents"] if bot["name"] == name))
    for iteration in range(2):
        wait_idle(relay)
        body = {"channel": "research", "content":
                " ".join("nostr:" + bot["npub"] for bot in bots) + f" Work together on team request {iteration}."}
        body.update({f"mention_{index}": bot["npub"] for index, bot in enumerate(bots)})
        if iteration: body["reply_to"] = root["id"]
        before = len(Endpoint.requests)
        relay.request("/api/event", body)
        if not iteration:
            root = next(event for event in relay.request("/api/events")["events"]
                        if event["content"] == body["content"])
        wait_request_count(before + 4)  # election, plan, writer, reviewer
        wait_idle(relay)
        requests = Endpoint.requests[before:]
        assert all(request[1]["model"] == "custom" for request in requests)
        assert "election committee" in requests[0][1]["messages"][0]["content"]
        assert "You are the leader" in requests[1][1]["messages"][0]["content"]
        assert all("ROOM_PROMPT_PROOF" in json.dumps(request[1]) for request in requests)
        replies = [event for event in relay.request("/api/events")["events"]
                   if event.get("reply_to") == root["id"] and "API_REPLY" in event["content"]]
        assert {bot["pubkey"] for bot in bots[1:]} <= {event["pubkey"] for event in replies}
        assert all(root["pubkey"] in event["mentions"] for event in replies)
    print("teams: election, planning, sequential handoff, selected API, room guidance, human ownership and repeat request OK")


def check_cline(relay, host):
    relay.stop()
    data = relay.directory / "cline-data"
    (data / "settings").mkdir(parents=True)
    (data / "settings/providers.json").write_text('{"activeProviderId":"fixture"}')
    relay.environment["CLINE_DATA_DIR"] = str(data)
    binary = relay.directory / "bin/cline"
    binary.write_text('''#!/usr/bin/env python3
import json, sys
assert '--json' in sys.argv and '--cwd' in sys.argv
prompt = sys.argv[-1]
print(json.dumps({'type':'say','say':'api_req_started','text':'PRIVATE_PROGRESS'}))
print(json.dumps({'type':'say','say':'text','text':'PARTIAL_THOUGHT','partial':True}))
print(json.dumps({'type':'say','say':'completion_result','text':'CLINE_REPLY_PROOF'}))
if 'CLI_FAILURE_CASE' in prompt:
    sys.exit(2)
''')
    binary.chmod(0o755)
    relay.start()
    relay.request("/api/identity", {"action": "create"})
    relay.request("/api/identity", {"action": "ack_backup", "save_pass": False})
    assert relay.request("/api/provider")["providers"]["cline"]["ready"]
    session = relay.request("/api/agent", {"name": "Cli helper", "provider": "cline",
                   "system_prompt": "Give a short answer", "save_pass": True})
    bot = next(bot for bot in session["agents"] if bot["name"] == "Cli helper")
    post_thread(relay, bot, "CLI_SUCCESS_CASE")
    reply = wait_reply(relay, "CLINE_REPLY_PROOF")
    assert reply["pubkey"] == bot["pubkey"] and reply["content"] == "CLINE_REPLY_PROOF"
    wait_idle(relay)
    request = post_thread(relay, bot, "CLI_FAILURE_CASE")
    wait_idle(relay)
    replies = [event for event in relay.request("/api/events")["events"]
               if event.get("reply_to") == request["id"]]
    assert any("did not return a usable reply" in event["content"] for event in replies)
    assert not any("CLINE_REPLY" in event["content"] or "PRIVATE_PROGRESS" in event["content"] for event in replies)
    print("Cline: CLI selection, complete JSON response, progress filtering and failed-exit suppression OK")


def check_slow_reader(relay):
    """A subscriber that stops reading is disconnected, never served torn frames."""
    wait_idle(relay)
    payload = "SLOW_READER_" + ("x" * 4000)
    for _ in range(80):
        relay.request("/api/event", {"channel": "research", "content": payload})
    slow = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
    try:
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        slow.settimeout(0)  # never read the responses
        # Each REQ replays up to 64 stored notes from this channel, so forty of
        # them push several megabytes at a socket that is not draining.
        slow.sendall(b"".join(
            f'["REQ","slow-{index}",{{"kinds":[1],"#h":["research"]}}]\n'.encode()
            for index in range(40)
        ))
        time.sleep(1.5)
        assert relay.request("/api/status")["ok"] is True
        slow.settimeout(10)
        data = b""
        closed = False
        try:
            while True:
                chunk = slow.recv(65536)
                if not chunk:
                    closed = True
                    break
                data += chunk
        except ConnectionResetError:
            closed = True
        assert closed, "relay kept a reader past its output queue"
        for line in data.split(b"\n")[:-1]:
            if line.strip():
                json.loads(line)
    finally:
        slow.close()
    print("backpressure: slow subscriber disconnected without torn frames OK")


def check_oversized_line(relay):
    """A line larger than the relay buffer gets a NOTICE before the close."""
    client = socket.create_connection(("127.0.0.1", relay.port), timeout=5)
    try:
        client.settimeout(5)
        try:
            client.sendall(b'["EVENT","' + b"x" * 40000)
        except OSError:
            pass
        data = b""
        try:
            while True:
                chunk = client.recv(65536)
                if not chunk:
                    break
                data += chunk
        except (ConnectionResetError, socket.timeout):
            pass
        assert b"line too long" in data, data[:120]
    finally:
        client.close()
    print("wire: oversized line gets a NOTICE before the close OK")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-collaboration-") as temporary:
        relay = Relay(Path(temporary))
        try:
            relay.start()
            check_rooms(relay)
            check_providers(relay)
            check_history_capacity(relay)
            check_slow_reader(relay)
            check_oversized_line(relay)
        except Exception:
            relay.log.flush()
            relay.log.seek(0)
            print("Relay exit:", relay.process.poll(), relay.log.read()[-2000:])
            raise
        finally:
            relay.stop()
            relay.log.close()


def check_history_capacity(relay):
    wait_idle(relay)
    last = ""
    for index in range(600):
        last = f"HISTORY_CAPACITY_{index}:" + "\t" * 4000
        relay.request("/api/event", {"channel": "general", "content": last})
    events = relay.request("/api/events")["events"]
    assert any(event["content"] == last for event in events)
    assert len(json.dumps(events)) > 2_000_000
    assert all(len(event["content"]) <= 4096 for event in events)
    print("history: complete multi-megabyte event response across socket backpressure OK")


def serve_ui_fixture():
    """A disposable browser workspace; lifetime belongs to its parent test process."""
    with tempfile.TemporaryDirectory(prefix="hush-ui-fixture-") as temporary:
        relay = Relay(Path(temporary))
        with ThreadingHTTPServer(("127.0.0.1", 0), Endpoint) as endpoint:
            threading.Thread(target=endpoint.serve_forever, daemon=True).start()
            try:
                relay.start()
                relay.request("/api/identity", {"action": "create"})
                relay.request("/api/identity", {"action": "ack_backup", "save_pass": False})
                relay.request("/api/vibe", {"name": "Hush Workspace", "about": "Local browser test"})
                relay.request("/api/provider", {"provider": "custom", "host": f"http://127.0.0.1:{endpoint.server_port}", "model": "ui_fixture"})
                for name in ("Avery", "Quinn"):
                    relay.request("/api/agent", {"name": name, "provider": "custom", "system_prompt": "Help turn ideas into practical next steps.", "save_pass": False, "intro_enabled": False})
                print(json.dumps({"url": f"http://127.0.0.1:{relay.port}"}), flush=True)
                sys.stdin.read()
            finally:
                relay.stop()
                endpoint.shutdown()


if __name__ == "__main__":
    if sys.argv[1:] == ["--serve-ui"]:
        serve_ui_fixture()
    else:
        main()
