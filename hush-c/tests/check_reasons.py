#!/usr/bin/env python3
"""Issue #210: every robot, loadout, and favorite refusal names its rule.

Each case posts a refused write to an isolated live relay and compares the
400 body byte for byte with the reason the C code sends. Rewording or
dropping a reason in src/api_agents.c or src/api_favorite.c fails here,
naming the case, the wanted line, and the line the relay sent. The UI half
checks that the robot drawer shows the server reason in #agent-err.
"""

import json
import os
import re
from html.parser import HTMLParser
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Exact reason lines (without the trailing newline the relay appends).
GATE = "Log in and set up your vibe before changing robots."
NAME = "Robot name is required."
NAME_CHARS = "Robot names need a letter (A-Z) or digit."
TAKEN = "A robot named Walkbot One already exists."
PROMPT = "System prompt is required."
NO_PROVIDER = "Provider is required."
PROVIDER = "Unknown AI provider. Choose one from the list."
MIN1 = "Keep at least one skill equipped."
ROLE = "Skill system:human-cue is for chaperon robots only."
BUDGET = "Loadout over budget: at most 8 skills, 8000 characters, complexity 64."
NO_FILES = "Ollama cannot read context files."
NO_FILES_GEMINI = "Gemini API cannot read context files."
FILES = "Context files must be plain text or Markdown, at most 4096 bytes each."
FULL = "Robot roster is full (16 robots)."
SLUG = "Robot id is required."
MISSING = "No robot with id ghost."
MAJOR = "Major cannot be deleted or cloned."
CLONE_LONG = ('Cannot clone {}: the name plus " copy" would be over 63 bytes; '
              "shorten the name first.")
FAV_ACTION = "Loadout action must be save, list, load or delete."
FAV_ROBOT = "Robot id must use a-z, 0-9, - or _ (1-63 characters)."
FAV_NAME = ("Favorite names use letters, digits, spaces, - or _ "
            "(1-47 characters, at least one letter or digit).")
FAV_IDS = "Skill ids must be strings of at most 95 characters."
FAV_EMPTY = "A favorite needs at least one skill."
FAV_MANY = "A favorite holds at most 8 skills."
FAV_CLASH = "That name clashes with a different saved favorite; pick another name."
FAV_FULL = "This robot already has 32 favorites; delete one first."
FAV_MISSING = "No favorite with that name for this robot."
FAV_CORRUPT = "That saved favorite file is corrupt."

# UI contract: api() keeps the 400 body, and the robot drawer shows it in
# the existing #agent-err line under the form (fallback copy unchanged).
UI_KEEPS_REASON = 'err.reason = r.status === 400 ? (await r.text().catch(() => "")).trim() : "";'
UI_SHOWS_REASON = '$("agent-err").textContent = (e && e.reason) || "Could not save that robot.";'
UI_NAME_RULE = '<p class="help" id="agent-name-rule">Names must differ in letters or digits.</p>'
# A long unbroken name wraps inside the drawer instead of clipping at 375.
UI_ERR_WRAP = "#agent-err { overflow-wrap: anywhere; }"
# The rule shows while the name is editable and hides when it is locked
# (Edit Major, Edit locked robot): reset shows it, each lock hides it.
UI_RULE_SHOWN = 'if ($("agent-name-rule")) $("agent-name-rule").hidden = false;'
UI_RULE_HIDDEN = 'if ($("agent-name-rule")) $("agent-name-rule").hidden = true;'
# Reason sources: no user-facing reason may say "slug".
REASON_SOURCES = ("src/api_agents.c", "src/api_favorite.c",
                  "include/hush_http_internal.h")

FAILURES = []


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


class Relay:
    def __init__(self, directory):
        self.directory = directory
        self.home = directory / "home"
        self.port = free_port()
        self.environment = dict(
            os.environ,
            HOME=str(directory),
            HUSH_HOME=str(self.home),
            HUSH_CONFIG_DIR=str(directory / "config"),
            PASSWORD_STORE_DIR=str(directory / "pass"),
            HUSH_PASS_HELPER=str(ROOT / "tests" / "fake-pass.sh"),
            HUSH_FAKE_PASS_DIR=str(directory / "fakepass"),
            HUSH_AUTO_UPDATE="0",
        )
        self.environment.pop("XDG_CONFIG_HOME", None)
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
        raise AssertionError("reasons: isolated relay did not start")

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
        """Returns (status, content type, raw body); retries a 429 burst."""
        # Compact separators: the relay's field reader matches "key":"value".
        # Raw UTF-8 like the browser's JSON.stringify, not \u escapes.
        payload = None if body is None else json.dumps(
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
        raise AssertionError(f"reasons: {path} stayed rate-limited")

    def ok(self, path, body):
        status, _, raw = self.call("POST", path, body)
        if status != 200:
            raise AssertionError(f"reasons: setup {path} {body} -> {status} {raw[:200]!r}")
        return json.loads(raw)


def expect(relay, case, path, body, reason):
    """Records a failure unless the relay answers 400 text/plain reason\\n."""
    status, ctype, raw = relay.call("POST", path, body)
    want = (reason + "\n").encode()
    if status == 400 and raw == want and ctype.startswith("text/plain"):
        print(f"reasons: ok {case}: {reason}")
        return
    FAILURES.append(
        f"{case}: want HTTP 400 text/plain {want!r}; "
        f"got HTTP {status} {ctype} {raw[:200]!r}")


def robot(name, **extra):
    body = {"name": name, "system_prompt": "Walk the floor.",
            "provider": "grok-build", "save_pass": False}
    body.update(extra)
    return body


def check_robot_gate(relay):
    expect(relay, "create before login", "/api/agent", robot("Walkbot One"), GATE)
    relay.ok("/api/identity", {"action": "create"})
    relay.ok("/api/identity", {"action": "ack_backup", "save_pass": False})
    expect(relay, "create before vibe", "/api/agent", robot("Walkbot One"), GATE)
    relay.ok("/api/vibe", {"name": "HQ", "about": "reasons"})


def check_robot_names(relay):
    made = relay.ok("/api/agent", robot("Walkbot One"))
    slugs = [a.get("slug") for a in made.get("agents", [])]
    if "walkbot-one" not in slugs:
        FAILURES.append(f"'Walkbot One' must be accepted as walkbot-one; got {slugs}")
    else:
        print("reasons: ok spaces allowed: 'Walkbot One' -> walkbot-one")
    expect(relay, "name clash", "/api/agent", robot("walkbot-one"), TAKEN)
    expect(relay, "name clash by case and punctuation", "/api/agent",
           robot("  WALKBOT one!! "), TAKEN)
    expect(relay, "blank name", "/api/agent", robot("   "), NAME)
    for case, name in (("bangs", "!!!"), ("symbols", "@#$%"),
                       ("emoji", "\U0001F916\U0001F916"),
                       ("Cyrillic letters", "\u0420\u043e\u0431\u043e\u0442")):
        expect(relay, f"name of only {case}", "/api/agent", robot(name), NAME_CHARS)
    status, _, raw = relay.call("GET", "/api/session")
    slugs = [a.get("slug") for a in json.loads(raw).get("agents", [])]
    if status != 200 or "a" in slugs or "" in slugs:
        FAILURES.append(f"symbol-only names must not make a robot; got {slugs}")
    else:
        print("reasons: ok symbol-only names made no robot (no slug a)")
    body = robot("x")
    del body["name"]
    expect(relay, "missing name", "/api/agent", body, NAME)
    expect(relay, "blank prompt", "/api/agent", robot("Pat", system_prompt=" "), PROMPT)
    body = robot("Pat")
    del body["system_prompt"]
    expect(relay, "missing prompt", "/api/agent", body, PROMPT)
    body = robot("Pat")
    del body["provider"]
    expect(relay, "missing provider", "/api/agent", body, NO_PROVIDER)


def check_robot_setup(relay):
    expect(relay, "unknown provider", "/api/agent", robot("Pat", provider="local"),
           PROVIDER)
    expect(relay, "unknown ranked provider", "/api/agent",
           robot("Pat", providers="grok-build,nope"), PROVIDER)
    expect(relay, "unknown provider is not echoed", "/api/agent",
           robot("Pat", provider="<b>x</b>"), PROVIDER)
    expect(relay, "unknown voice", "/api/agent", robot("Pat", voice="zzz"),
           "Unknown voice: zzz.")
    expect(relay, "provider cannot read files", "/api/agent",
           robot("Pat", provider="ollama", context_name_0="brief.md",
                 context_mime_0="text/markdown", context_text_0="hi"), NO_FILES)
    expect(relay, "provider label, not id", "/api/agent",
           robot("Pat", provider="gemini-api", context_name_0="notes.txt",
                 context_mime_0="text/plain", context_text_0="hi"), NO_FILES_GEMINI)
    expect(relay, "context not text", "/api/agent",
           robot("Pat", context_name_0="a.png", context_mime_0="image/png",
                 context_text_0="x"), FILES)


def forge(relay, name, **extra):
    body = {"name": name, "summary": "reasons", "body": "Say hi."}
    body.update(extra)
    return relay.ok("/api/skill", body)["id"]


def check_robot_loadout(relay, owned):
    expect(relay, "empty loadout", "/api/agent",
           robot("Pat", skill_0="", nskills=0), MIN1)
    expect(relay, "unknown skill", "/api/agent", robot("Pat", skill_0="system:nope"),
           "Unknown skill: system:nope.")
    expect(relay, "role wall", "/api/agent",
           robot("Pat", skill_0="system:human-cue"), ROLE)
    expect(relay, "other robot's skill", "/api/agent", robot("Pat", skill_0=owned),
           f"Skill {owned} belongs to another robot.")
    big = "x" * 3900
    heavy = [forge(relay, f"heavy {i}", body=big) for i in range(3)]
    extra = {f"skill_{i}": sid for i, sid in enumerate(heavy)}
    expect(relay, "loadout over budget", "/api/agent", robot("Pat", **extra), BUDGET)


def check_robot_slugs(relay):
    expect(relay, "update without slug", "/api/agent", {"action": "update"}, SLUG)
    expect(relay, "update missing robot", "/api/agent",
           {"action": "update", "slug": "ghost", "name": "G"}, MISSING)
    expect(relay, "update bad provider", "/api/agent",
           {"action": "update", "slug": "walkbot-one", "provider": "nope"}, PROVIDER)
    expect(relay, "delete without slug", "/api/agent", {"action": "delete"}, SLUG)
    expect(relay, "delete missing robot", "/api/agent",
           {"action": "delete", "slug": "ghost"}, MISSING)
    expect(relay, "clone missing robot", "/api/agent",
           {"action": "clone", "slug": "ghost"}, MISSING)
    expect(relay, "delete Major", "/api/agent",
           {"action": "delete", "slug": "sgt-major-payne"}, MAJOR)
    expect(relay, "clone Major", "/api/agent",
           {"action": "clone", "slug": "sgt-major-payne"}, MAJOR)
    check_robot_clones(relay)


def check_robot_clones(relay):
    """Clones are named "<name> copy"; names hold at most 63 bytes."""
    edge = "Edge " + "x" * 53  # 58 bytes: "<name> copy" is exactly 63
    relay.ok("/api/agent", robot(edge))
    made = relay.ok("/api/agent", {"action": "clone", "slug": "edge-" + "x" * 53})
    slugs = [a.get("slug") for a in made.get("agents", [])]
    if "edge-" + "x" * 53 + "-copy" not in slugs:
        FAILURES.append(f"a 58-byte name must clone (63-byte copy name); got {slugs}")
    else:
        print("reasons: ok clone at the limit: 58-byte name -> 63-byte copy name")
    long_slug = "long-" + "x" * 54
    relay.ok("/api/agent", robot("Long " + "x" * 54))  # 59 bytes: copy is 64
    expect(relay, "clone name too long", "/api/agent",
           {"action": "clone", "slug": long_slug}, CLONE_LONG.format("Long " + "x" * 54))
    relay.ok("/api/agent", robot("Twin"))
    relay.ok("/api/agent", {"action": "clone", "slug": "twin"})
    expect(relay, "clone twice", "/api/agent", {"action": "clone", "slug": "twin"},
           "A robot named Twin copy already exists.")


def agent_in_memory(relay, slug):
    """Every field the relay reports for slug, from GET /api/session."""
    status, _, raw = relay.call("GET", "/api/session")
    if status != 200:
        raise AssertionError(f"reasons: GET /api/session -> {status}")
    for agent in json.loads(raw).get("agents", []):
        if agent.get("slug") == slug:
            return agent
    raise AssertionError(f"reasons: robot {slug} missing from the session")


def agent_on_disk(relay, slug):
    """The persisted fields for slug in vibe.json, with the index stripped."""
    vibe = json.loads((relay.directory / "config" / "vibe.json").read_text())
    for i in range(int(vibe.get("nagents", 0))):
        if vibe.get(f"agent_slug_{i}") != slug:
            continue
        fields = {}
        for key, value in vibe.items():
            if re.fullmatch(rf"agent_[a-z_]+_{i}", key):
                fields[key[:-len(f"_{i}")]] = value
            elif key.startswith(f"agent_{i}_"):
                fields["agent_" + key[len(f"agent_{i}_"):]] = value
        return fields
    raise AssertionError(f"reasons: robot {slug} missing from vibe.json")


def refused(relay, case, body):
    """A 400 whose reason is still plain (unknown role on update: #233)."""
    status, _, raw = relay.call("POST", "/api/agent", body)
    if status == 400:
        print(f"reasons: ok {case}: HTTP 400")
    else:
        FAILURES.append(f"{case}: want HTTP 400; got HTTP {status} {raw[:200]!r}")


def same_fields(case, before, after):
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    if changed:
        FAILURES.append(f"{case}: refused updates changed {changed}: "
                        f"before {[before.get(k) for k in changed]!r}, "
                        f"after {[after.get(k) for k in changed]!r}")
    else:
        print(f"reasons: ok {case}: all {len(before)} fields unchanged")


def check_robot_rename(relay):
    """Rename keeps the name rule, keeps the slug, and a 400 writes nothing."""
    relay.ok("/api/agent", robot("Steady"))
    memory = agent_in_memory(relay, "steady")
    disk = agent_on_disk(relay, "steady")
    update = {"action": "update", "slug": "steady",
              "name": "Steady Two", "system_prompt": "Changed."}
    expect(relay, "update refused provider", "/api/agent",
           dict(update, provider="nope"), PROVIDER)
    expect(relay, "update refused voice", "/api/agent",
           dict(update, provider="ollama", voice="zzz"), "Unknown voice: zzz.")
    refused(relay, "update refused role", dict(update, role="boss"))
    expect(relay, "rename blank", "/api/agent", dict(update, name="   "), NAME)
    expect(relay, "rename name clash", "/api/agent", dict(update, name="walkbot ONE"),
           TAKEN)
    expect(relay, "rename to symbols only", "/api/agent", dict(update, name="???"),
           NAME_CHARS)
    expect(relay, "rename to Cyrillic letters only", "/api/agent",
           dict(update, name="\u0420\u043e\u0431\u043e\u0442"), NAME_CHARS)
    same_fields("refused updates in memory", memory, agent_in_memory(relay, "steady"))
    relay.ok("/api/agent", robot("Probe"))  # any later save rewrites vibe.json
    same_fields("refused updates on disk", disk, agent_on_disk(relay, "steady"))
    relay.ok("/api/agent", {"action": "update", "slug": "steady", "name": "STEADY!"})
    relay.ok("/api/agent", {"action": "update", "slug": "steady", "name": "Calm Hand"})
    agent = agent_in_memory(relay, "steady")
    if (agent.get("name"), agent.get("slug")) != ("Calm Hand", "steady"):
        FAILURES.append(f"rename must keep the slug: got {agent.get('name')!r} "
                        f"slug {agent.get('slug')!r}")
    else:
        print("reasons: ok rename to own slug and to a new name: slug stays steady")


def check_context_size(relay):
    """A context text is refused past 4096 bytes, not silently cut."""
    files = {"context_name_0": "brief.md", "context_mime_0": "text/markdown"}
    made = relay.ok("/api/agent", robot("Ctx Exact", context_text_0="x" * 4096, **files))
    exact = [a for a in made.get("agents", []) if a.get("slug") == "ctx-exact"]
    if not exact or exact[0].get("ncontext") != 1:
        FAILURES.append(f"a 4096-byte context file must be accepted; got {exact}")
    else:
        print("reasons: ok context at the limit: 4096 bytes accepted")
    expect(relay, "context 4097 bytes", "/api/agent",
           robot("Pat", context_text_0="x" * 4097, **files), FILES)
    expect(relay, "context 20000 bytes", "/api/agent",
           robot("Pat", context_text_0="x" * 20000, **files), FILES)


def check_robot_full(relay):
    session = relay.ok("/api/agent", robot("Walkbot Two"))
    count = len(session.get("agents", []))
    for i in range(count, 16):
        relay.ok("/api/agent", robot(f"Filler {i}"))
    expect(relay, "roster full", "/api/agent", robot("Walkbot Extra"), FULL)
    # Clone checks run in roster order: name too long first, then full.
    long_slug = "long-" + "x" * 54
    expect(relay, "clone too long while full", "/api/agent",
           {"action": "clone", "slug": long_slug}, CLONE_LONG.format("Long " + "x" * 54))
    expect(relay, "clone while full", "/api/agent",
           {"action": "clone", "slug": "walkbot-one"}, FULL)


def fav(action, **extra):
    body = {"action": action, "robot": "walkbot-one"}
    body.update(extra)
    return body


def check_favorite_inputs(relay, owned):
    expect(relay, "favorite bad action", "/api/loadout", fav("zzz"), FAV_ACTION)
    expect(relay, "favorite no action", "/api/loadout", {"robot": "walkbot-one"},
           FAV_ACTION)
    expect(relay, "favorite robot with space", "/api/loadout",
           fav("list", robot="Walkbot One"), FAV_ROBOT)
    expect(relay, "favorite dotted name", "/api/loadout",
           fav("save", name="a.b", skill_0="system:hive-patterns"), FAV_NAME)
    expect(relay, "favorite name without letters", "/api/loadout",
           fav("save", name="- _", skill_0="system:hive-patterns"), FAV_NAME)
    expect(relay, "favorite load bad name", "/api/loadout", fav("load", name="a%b"),
           FAV_NAME)
    expect(relay, "favorite non-string skill", "/api/loadout",
           fav("save", name="ok", skill_0=5), FAV_IDS)
    expect(relay, "favorite ninth skill", "/api/loadout",
           fav("save", name="ok", skill_8="system:hive-patterns"), FAV_MANY)
    expect(relay, "favorite no skills", "/api/loadout", fav("save", name="ok"), FAV_EMPTY)
    expect(relay, "favorite unknown skill", "/api/loadout",
           fav("save", name="ok", skill_0="system:nope"), "Unknown skill: system:nope.")
    expect(relay, "favorite other robot's skill", "/api/loadout",
           fav("save", name="ok", skill_0=owned),
           f"Skill {owned} belongs to another robot.")
    expect(relay, "favorite repeated skill", "/api/loadout",
           fav("save", name="ok", skill_0="system:hive-patterns",
               skill_1="system:hive-patterns"),
           "Skill system:hive-patterns is listed twice.")


def check_favorite_store(relay):
    one = {"skill_0": "system:hive-patterns"}
    relay.ok("/api/loadout", fav("save", name="My Fav", **one))
    expect(relay, "favorite slug clash", "/api/loadout",
           fav("save", name="my-fav", **one), FAV_CLASH)
    expect(relay, "favorite load missing", "/api/loadout", fav("load", name="ghost"),
           FAV_MISSING)
    expect(relay, "favorite delete missing", "/api/loadout",
           fav("delete", name="ghost"), FAV_MISSING)
    loadouts = relay.home / "robots" / "walkbot-one" / "loadouts"
    (loadouts / "broken.json").write_text("{not json")
    expect(relay, "favorite corrupt file", "/api/loadout", fav("load", name="broken"),
           FAV_CORRUPT)
    (loadouts / "broken.json").unlink()
    for i in range(31):
        relay.ok("/api/loadout", fav("save", name=f"fav {i:02d}", **one))
    expect(relay, "favorites full", "/api/loadout",
           fav("save", name="one too many", **one), FAV_FULL)


class Placement(HTMLParser):
    """Records the open-element ids around #agent-name-rule and whether
    #agent-name came first inside the same .field."""

    VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
            "meta", "source", "track", "wbr"}

    def __init__(self):
        super().__init__()
        self.stack = []
        self.rule_parents = None
        self.input_field = None
        self.rule_field = None

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        node = (tag, attrs.get("id") or "", attrs.get("class") or "", self.getpos())
        field = next((n for n in reversed(self.stack) if "field" in n[2].split()), None)
        if node[1] == "agent-name" and tag == "input":
            self.input_field = field
        if node[1] == "agent-name-rule" and self.rule_parents is None:
            self.rule_parents = [n[1] for n in self.stack if n[1]]
            self.rule_field = field if self.input_field is not None else None
        if tag not in self.VOID:
            self.stack.append(node)

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                del self.stack[i:]
                return


def name_rule_placement(text):
    """Empty when #agent-name-rule sits in #agent-identity after #agent-name."""
    placement = Placement()
    placement.feed(text)
    if placement.rule_parents is None:
        return "no #agent-name-rule"
    if "agent-identity" not in placement.rule_parents:
        return f"#agent-name-rule outside #agent-identity (inside {placement.rule_parents})"
    if placement.rule_field is None or placement.rule_field != placement.input_field:
        return "#agent-name-rule must follow the #agent-name input in its .field"
    return ""


def reason_strings():
    """Every quoted string in the refusal-reason #defines."""
    found = []
    for rel in REASON_SOURCES:
        text = (ROOT / rel).read_text()
        for match in re.finditer(r"#define\s+HUSH_\w*WHY_\w+((?:[^\n]*\\\n)*[^\n]*)", text):
            found.append("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))))
    return found


def check_ui(relay):
    demo = (ROOT / "demo" / "index.html").read_text()
    _, _, served = relay.call("GET", "/")
    served = served.decode("utf-8", "replace")
    for label, text in (("demo/index.html", demo), ("served UI", served)):
        for need in (UI_KEEPS_REASON, UI_SHOWS_REASON, UI_NAME_RULE, UI_ERR_WRAP,
                     UI_RULE_SHOWN):
            if need in text:
                print(f"reasons: ok {label} has {need[:48]}... ({len(need)} chars)")
            else:
                FAILURES.append(f"{label} must contain {need!r} so the robot "
                                "drawer shows the server reason in #agent-err")
        hidden = text.count(UI_RULE_HIDDEN)
        if hidden == 2:
            print(f"reasons: ok {label} hides #agent-name-rule for Major and locked robots")
        else:
            FAILURES.append(f"{label} must hide #agent-name-rule in both name locks "
                            f"(Major, locked robot); found {hidden}")
        problem = name_rule_placement(text)
        if problem:
            FAILURES.append(f"{label}: {problem}")
        else:
            print(f"reasons: ok {label} has #agent-name-rule in #agent-identity "
                  "after the name input")
    words = [UI_NAME_RULE] + reason_strings()
    slugged = [w for w in words if re.search(r"slug", w, re.IGNORECASE)]
    if slugged or len(words) < 30:
        FAILURES.append(f"no user-facing reason may say slug: {slugged} "
                        f"({len(words)} strings read)")
    else:
        print(f"reasons: ok no reason or help line says slug ({len(words)} strings)")


def main():
    with tempfile.TemporaryDirectory(prefix="hush-reasons-") as raw:
        relay = Relay(Path(raw))
        relay.start()
        try:
            check_robot_gate(relay)
            check_robot_names(relay)
            check_robot_setup(relay)
            owned = forge(relay, "owned elsewhere", scope="robot", robot="other-bot")
            check_robot_loadout(relay, owned)
            check_robot_slugs(relay)
            check_robot_rename(relay)
            check_context_size(relay)
            check_favorite_inputs(relay, owned)
            check_favorite_store(relay)
            check_robot_full(relay)
            check_ui(relay)
        finally:
            relay.stop()
    if FAILURES:
        for line in FAILURES:
            print(f"reasons check failed: {line}", file=sys.stderr)
        sys.exit(1)
    print("refusal reasons ok")


if __name__ == "__main__":
    main()
