#!/usr/bin/env python3
"""Issue #210: every robot, loadout, and favorite refusal names its rule.

Each case posts a refused write to an isolated live relay and compares the
400 body byte for byte with the reason the C code sends. Rewording or
dropping a reason in src/api_agents.c or src/api_favorite.c fails here,
naming the case, the wanted line, and the line the relay sent. The UI half
checks that the robot drawer shows the server reason in #agent-err, and that
clone, delete, Save Major, and favorite save/delete use that same expression.
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
NAME_PRINT = ("Robot names cannot include line breaks or other characters that do not print.")
TAKEN = "A robot named Walkbot One already exists."
# A name that is not the same but reads the same in A-Z letters and digits.
LIKE = "Too close to {}: names must differ in letters (A-Z) or 0-9."
PROMPT = "System prompt is required."
NO_PROVIDER = "Provider is required."
PROVIDER = "Unknown AI provider. Choose one from the list."
MIN1 = "Keep at least one skill equipped."
ROLE = "Skill system:human-cue is for chaperon robots only."
BUDGET = "Loadout over budget: at most 8 skills, 8000 characters, complexity 64."
NO_FILES = "Ollama cannot read context files."
NO_FILES_GEMINI = "Gemini API cannot read context files."
NO_FILES_DEEPSEEK = "DeepSeek API cannot read context files."
# The provider is spelled DeepSeek everywhere it is shown.
UI_DEEPSEEK = ('value="deepseek-api"> DeepSeek API</label>',
               '"deepseek-api": "DeepSeek API",')
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
UI_CLONE_REASON = '$("agent-err").textContent = (e && e.reason) || "Could not clone that robot.";'
UI_DELETE_REASON = '$("agent-err").textContent = (e && e.reason) || "Could not delete that robot.";'
UI_MAJOR_REASON = '$("agent-err").textContent = (e && e.reason) || "Could not save Major.";'
UI_FAV_SAVE_REASON = 'skillNotice((e && e.reason) || "Could not save that favorite.");'
UI_FAV_DELETE_REASON = 'skillNotice((e && e.reason) || "Could not delete that favorite.");'
UI_NAME_RULE = ('<p class="help" id="agent-name-rule">'
                'Names must differ in letters (A-Z) or 0-9.</p>')
UI_NAME_HELP = ('<p class="help" id="agent-name-help">'
                "State the robot\u2019s name. Leave blank to auto-generate one.</p>")
# A long unbroken name wraps inside the drawer instead of clipping at 375.
UI_ERR_WRAP = "#agent-err { overflow-wrap: anywhere; }"
# The rule shows while the name is editable and hides when it is locked
# (Edit Major, Edit locked robot): reset shows it, each lock hides it.
UI_RULE_SHOWN = 'if ($("agent-name-rule")) $("agent-name-rule").hidden = false;'
UI_RULE_HIDDEN = 'if ($("agent-name-rule")) $("agent-name-rule").hidden = true;'
# "Leave blank to auto-generate one" follows the same rule: shown on create
# and edit, hidden while the name is locked.
UI_HELP_SHOWN = 'if ($("agent-name-help")) $("agent-name-help").hidden = false;'
UI_HELP_HIDDEN = 'if ($("agent-name-help")) $("agent-name-help").hidden = true;'
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
        # bytes go out as they are (raw control bytes a browser never sends).
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
    expect(relay, "name clash, same name", "/api/agent", robot("  Walkbot One "), TAKEN)
    expect(relay, "name clash", "/api/agent", robot("walkbot-one"),
           LIKE.format("Walkbot One"))
    expect(relay, "name clash by case and punctuation", "/api/agent",
           robot("  WALKBOT one!! "), LIKE.format("Walkbot One"))
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
    expect(relay, "provider label spelled DeepSeek", "/api/agent",
           robot("Pat", provider="deepseek-api", context_name_0="notes.txt",
                 context_mime_0="text/plain", context_text_0="hi"), NO_FILES_DEEPSEEK)
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
    check_ninth_skill(relay, heavy[0])


def check_ninth_skill(relay, one):
    """skill_8 and nskills over 8 are the budget 400, not a silent drop."""
    ids = [forge(relay, f"ninth {i}") for i in range(9)]
    extra = {f"skill_{i}": sid for i, sid in enumerate(ids)}
    expect(relay, "create skill_8", "/api/agent", robot("Nine", **extra), BUDGET)
    expect(relay, "create nskills over 8", "/api/agent",
           robot("Nine Count", skill_0=one, nskills=9), BUDGET)
    status, _, raw = relay.call("GET", "/api/session")
    slugs = [a.get("slug") for a in json.loads(raw).get("agents", [])]
    if status != 200 or "nine" in slugs or "nine-count" in slugs:
        FAILURES.append(f"skill_8 and nskills 9 must not create a robot; got {slugs}")
    else:
        print("reasons: ok ninth skill did not create a robot")
    status, _, raw = relay.call("GET", "/api/skills")
    listed = {s.get("id") for s in json.loads(raw).get("skills", [])}
    missing = [sid for sid in ids if sid not in listed]
    if status != 200 or missing:
        FAILURES.append(f"forged skills must stay created; missing {missing}")
    else:
        print("reasons: ok forged skills stayed in the catalog")
    before = agent_in_memory(relay, "walkbot-one")
    expect(relay, "update skill_8", "/api/agent",
           {"action": "update", "slug": "walkbot-one",
            "skill_0": ids[0], "skill_8": ids[8]}, BUDGET)
    expect(relay, "update nskills over 8", "/api/agent",
           {"action": "update", "slug": "walkbot-one",
            "skill_0": ids[0], "nskills": 9}, BUDGET)
    after = agent_in_memory(relay, "walkbot-one")
    if before.get("skills") != after.get("skills") or before.get("name") != after.get("name"):
        FAILURES.append("a refused ninth skill must leave the robot unchanged: "
                        f"{before.get('skills')} -> {after.get('skills')}")
    else:
        print("reasons: ok skill_8 update left the robot unchanged")
    made = relay.ok("/api/agent", robot("Cap Eight", skill_0=one, nskills=8))
    slugs = [a.get("slug") for a in made.get("agents", [])]
    if "cap-eight" not in slugs:
        FAILURES.append(f"nskills 8 with one skill must still create; got {slugs}")
    else:
        kept = agent_in_memory(relay, "cap-eight").get("skills")
        if kept != [one]:
            FAILURES.append(f"nskills 8 must keep the one skill, not invent more; got {kept}")
        else:
            print("reasons: ok nskills 8 with one skill still creates")


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
    # Walkbot One is created in check_robot_names on this same relay.
    relay.ok("/api/agent", {"action": "clone", "slug": "walkbot-one"})
    expect(relay, "clone Walkbot One twice", "/api/agent",
           {"action": "clone", "slug": "walkbot-one"},
           "A robot named Walkbot One copy already exists.")
    # Drop the copy so later creates still have a free roster slot.
    relay.ok("/api/agent", {"action": "delete", "slug": "walkbot-one-copy"})


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
    # A robot's own name never clashes with itself, so the next rule is named.
    expect(relay, "rename close to its own name, next rule named", "/api/agent",
           dict(update, name="STEADY!", provider="nope"), PROVIDER)
    expect(relay, "update refused voice", "/api/agent",
           dict(update, provider="ollama", voice="zzz"), "Unknown voice: zzz.")
    refused(relay, "update refused role", dict(update, role="boss"))
    expect(relay, "rename blank", "/api/agent", dict(update, name="   "), NAME)
    expect(relay, "rename name clash", "/api/agent", dict(update, name="walkbot ONE"),
           LIKE.format("Walkbot One"))
    expect(relay, "rename to the same name", "/api/agent",
           dict(update, name="Walkbot One"), TAKEN)
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


def session_names(relay):
    status, _, raw = relay.call("GET", "/api/session")
    if status != 200:
        raise AssertionError(f"reasons: GET /api/session -> {status}")
    return {a.get("slug"): a.get("name") for a in json.loads(raw).get("agents", [])}


def check_names(case, relay, want):
    names = session_names(relay)
    wrong = {slug: names.get(slug) for slug, name in want.items() if names.get(slug) != name}
    if wrong:
        FAILURES.append(f"{case}: want {want}; got {wrong}")
    else:
        print(f"reasons: ok {case}: {want}")


def check_name_rule(relay):
    """Names clash on the current names of other robots, never on ids: a
    renamed robot keeps its id, and a new robot whose id is held gets -2."""
    relay.ok("/api/agent", robot("Walkbot Two"))
    rename = {"action": "update", "slug": "walkbot-two"}
    calm = "A robot named Calm Hand already exists."
    expect(relay, "rename to a name another robot has", "/api/agent",
           dict(rename, name="Calm Hand"), calm)
    expect(relay, "create a name another robot has", "/api/agent", robot("Calm Hand"), calm)
    relay.ok("/api/agent", dict(rename, name="Quiet Hand"))
    relay.ok("/api/agent", robot("Walkbot Two"))
    check_names("a renamed robot's old name is free; its id gets -2", relay,
                {"walkbot-two": "Quiet Hand", "walkbot-two-2": "Walkbot Two"})
    expect(relay, "create the same name twice", "/api/agent", robot("Walkbot Two"),
           "A robot named Walkbot Two already exists.")
    expect(relay, "rename too close to another name", "/api/agent",
           dict(rename, name="walkbot two!"), LIKE.format("Walkbot Two"))
    for first, second, slug in (("\u0420\u043e\u0431\u043e\u0442 1", "\u0418\u0432\u0430\u043d 1", "1"),
                                ("\u0418\u0432\u0430\u043d 2", "\u041f\u0451\u0442\u0440 2", "2"),
                                ("Caf\u00e9", "Caf\u00e8", "caf"),
                                ("Zo", "Zo\u00eb", "zo")):
        relay.ok("/api/agent", robot(first))
        expect(relay, f"only A-Z letters and digits count: {second} vs {first}",
               "/api/agent", robot(second), LIKE.format(first))
        relay.ok("/api/agent", {"action": "delete", "slug": slug})
    expect(relay, "create Major's name", "/api/agent", robot("Major"),
           "A robot named Major already exists.")
    expect(relay, "rename to Major's name", "/api/agent",
           dict(rename, name="Major"), "A robot named Major already exists.")
    expect(relay, "rename too close to Major's name", "/api/agent",
           dict(rename, name="ma jor"), LIKE.format("Major"))
    relay.ok("/api/agent", robot("Sgt Major Payne"))
    check_names("Major's reserved id is never given out", relay,
                {"sgt-major-payne-2": "Sgt Major Payne"})
    relay.ok("/api/agent", {"action": "delete", "slug": "sgt-major-payne-2"})
    # A raw control byte in the posted name (not a JSON escape) is
    # refused before it is saved. GET /api/session stays valid JSON.
    raw_name = (b'{"name":"Ctl\x01Line\nBot","system_prompt":"Walk the floor.",'
                b'"provider":"grok-build","save_pass":false}')
    expect(relay, "raw control byte in a robot name", "/api/agent", raw_name,
           NAME_PRINT)
    status, _, raw = relay.call("GET", "/api/session")
    try:
        session = json.loads(raw)
    except json.JSONDecodeError as err:
        FAILURES.append(f"GET /api/session after a refused control name "
                        f"is not JSON: {err}; HTTP {status} {raw[:120]!r}")
        session = None
    if session is not None:
        names = [a.get("name") for a in session.get("agents", [])]
        hidden = [n for n in names if n and ("Ctl" in n or "\x01" in n)]
        if status != 200 or hidden:
            FAILURES.append(f"refused control name must not be saved; "
                            f"HTTP {status} names {names}")
        else:
            print("reasons: ok GET /api/session is JSON and the control name was not saved")
    expect(relay, "rename to a raw control byte", "/api/agent",
           b'{"action":"update","slug":"walkbot-two","name":"Quiet\x01Hand"}',
           NAME_PRINT)
    check_names("rename to a control byte writes nothing", relay,
                {"walkbot-two": "Quiet Hand"})
    # Browser paste / JSON.stringify: controls arrive as \t or \u00XX, not
    # raw bytes. The field reader must decode them so the print check sees
    # the control and refuses — otherwise the name saves as NighttWatch /
    # Bellu0007Bot / Escu001bBot / Nulu0000Bot.
    escaped = (
        ("tab short escape",
         b'{"name":"Night\\tWatch","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "NighttWatch"),
        ("tab unicode escape",
         b'{"name":"Night\\u0009Watch","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "NighttWatch"),
        ("bell unicode escape",
         b'{"name":"Bell\\u0007Bot","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "Bellu0007Bot"),
        ("esc unicode escape",
         b'{"name":"Esc\\u001bBot","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "Escu001bBot"),
        ("nul unicode escape",
         b'{"name":"Nul\\u0000Bot","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "Nulu0000Bot"),
        ("del unicode escape",
         b'{"name":"Del\\u007fBot","system_prompt":"Walk the floor.",'
         b'"provider":"grok-build","save_pass":false}',
         "Delu007fBot"),
    )
    for label, body, mangled in escaped:
        expect(relay, f"create with JSON-escaped control: {label}",
               "/api/agent", body, NAME_PRINT)
    status, _, raw = relay.call("GET", "/api/session")
    try:
        session = json.loads(raw)
    except json.JSONDecodeError as err:
        FAILURES.append(f"GET /api/session after JSON-escaped controls "
                        f"is not JSON: {err}; HTTP {status} {raw[:120]!r}")
        session = None
    if session is not None:
        names = [a.get("name") for a in session.get("agents", [])]
        bad = [n for n in names if n in (
            "NighttWatch", "Bellu0007Bot", "Escu001bBot", "Nulu0000Bot",
            "Delu007fBot", "Night\tWatch")]
        if status != 200 or bad:
            FAILURES.append(f"JSON-escaped controls must not be saved; "
                            f"HTTP {status} names {names} bad {bad}")
        else:
            print("reasons: ok JSON-escaped controls refused; session JSON ok")
    for label, create_body, mangled in escaped:
        # Same escapes on rename of walkbot-two.
        name_part = create_body.split(b'"name":"', 1)[1].split(b'","system_prompt"', 1)[0]
        rename = (b'{"action":"update","slug":"walkbot-two","name":"' +
                  name_part + b'"}')
        expect(relay, f"rename with JSON-escaped control: {label}",
               "/api/agent", rename, NAME_PRINT)
    check_names("JSON-escaped rename writes nothing", relay,
                {"walkbot-two": "Quiet Hand"})
    check_name_key(relay)
    check_rename_ids(relay)
    check_suffixed_favorites(relay)


def check_name_key(relay):
    """The clash key is lowercase A-Z and 0-9 only: every other byte is
    dropped and leaves no word break, so the help line is literal."""
    for name in ("WalkbotOne", "Walk-bot One", "W.a.l.k.b.o.t One", "Walkbot O'ne",
                 "Walkbot\u00e9One", "Walkbot\u0418One"):
        expect(relay, f"no word breaks: {name} vs Walkbot One", "/api/agent",
               robot(name), LIKE.format("Walkbot One"))
    relay.ok("/api/agent", robot("Robot 1"))
    for name in ("Robot1", "Ro bot 1"):
        expect(relay, f"no word breaks: {name} vs Robot 1", "/api/agent",
               robot(name), LIKE.format("Robot 1"))
    relay.ok("/api/agent", {"action": "delete", "slug": "robot-1"})
    for name in ("Ma jor", "Ma-jor"):
        expect(relay, f"no word breaks: {name} vs Major", "/api/agent",
               robot(name), LIKE.format("Major"))


def check_rename_ids(relay):
    """Rename never compares ids: Delta is free while a robot named Echo
    holds the id delta."""
    relay.ok("/api/agent", robot("Delta"))
    relay.ok("/api/agent", {"action": "update", "slug": "delta", "name": "Echo"})
    relay.ok("/api/agent", robot("Foxtrot"))
    relay.ok("/api/agent", {"action": "update", "slug": "foxtrot", "name": "Delta"})
    check_names("rename onto a name whose id is held", relay,
                {"delta": "Echo", "foxtrot": "Delta"})
    for slug in ("delta", "foxtrot"):
        relay.ok("/api/agent", {"action": "delete", "slug": slug})


def favorite_names(relay, slug):
    listed = relay.ok("/api/loadout", {"action": "list", "robot": slug})
    return [f.get("name") for f in listed.get("favorites", [])]


def check_suffixed_favorites(relay):
    """Favorites live under the robot's id, so a -2 robot keeps its own."""
    one = {"skill_0": "system:hive-patterns"}
    relay.ok("/api/loadout", fav("save", robot="walkbot-two-2", name="Two Fav", **one))
    loaded = relay.ok("/api/loadout", fav("load", robot="walkbot-two-2", name="Two Fav"))
    saved = sorted(p.name for p in (relay.home / "robots" / "walkbot-two-2" / "loadouts").glob("*.json"))
    owner = favorite_names(relay, "walkbot-two-2")
    other = favorite_names(relay, "walkbot-two")
    if owner != ["Two Fav"] or other or not saved or loaded.get("skills") != ["system:hive-patterns"]:
        FAILURES.append(f"favorites on walkbot-two-2: listed {owner}, walkbot-two {other}, "
                        f"files {saved}, load {loaded}")
    else:
        print(f"reasons: ok favorites on a -2 robot: saved, listed, loaded ({saved})")
    relay.ok("/api/loadout", fav("delete", robot="walkbot-two-2", name="Two Fav"))
    if favorite_names(relay, "walkbot-two-2"):
        FAILURES.append("favorite delete on walkbot-two-2 left it listed")
    else:
        print("reasons: ok favorites on a -2 robot: deleted")


def set_names(relay, names):
    """Rewrites stored robot names in vibe.json while the relay is down,
    as an older build could have saved them."""
    path = relay.directory / "config" / "vibe.json"
    vibe = json.loads(path.read_text())
    for i in range(int(vibe.get("nagents", 0))):
        slug = vibe.get(f"agent_slug_{i}")
        if slug in names:
            vibe[f"agent_name_{i}"] = names[slug]
    path.write_text(json.dumps(vibe, separators=(",", ":"), ensure_ascii=False))


def check_restart():
    """Ids and favorites survive a restart; names saved by an older build
    that break the rule can still be saved unchanged, never taken again."""
    with tempfile.TemporaryDirectory(prefix="hush-reasons-restart-") as raw:
        relay = Relay(Path(raw))
        relay.start()
        try:
            relay.ok("/api/identity", {"action": "create"})
            relay.ok("/api/identity", {"action": "ack_backup", "save_pass": True})
            relay.ok("/api/vibe", {"name": "HQ", "about": "restart"})
            relay.ok("/api/agent", robot("Walkbot Two"))
            relay.ok("/api/agent", {"action": "update", "slug": "walkbot-two",
                                    "name": "Quiet Hand"})
            relay.ok("/api/agent", robot("Walkbot Two"))
            relay.ok("/api/loadout", fav("save", robot="walkbot-two-2", name="Keep Fav",
                                         skill_0="system:hive-patterns"))
            relay.ok("/api/agent", robot("Old Bangs"))
            relay.ok("/api/agent", robot("Old Twin"))
            relay.ok("/api/agent", robot("Old Major"))
            relay.ok("/api/agent", robot("Old Key"))
            relay.stop()
            set_names(relay, {"old-bangs": "!!!", "old-twin": "Quiet Hand",
                              "old-major": "Major", "old-key": "QuietHand"})
            relay.start()
            check_names("restart keeps ids and names", relay,
                        {"walkbot-two": "Quiet Hand", "walkbot-two-2": "Walkbot Two"})
            kept = favorite_names(relay, "walkbot-two-2")
            if kept != ["Keep Fav"]:
                FAILURES.append(f"restart must keep walkbot-two-2's favorite; got {kept}")
            else:
                print("reasons: ok restart keeps a -2 robot's favorites")
            bangs = {"action": "update", "slug": "old-bangs"}
            expect(relay, "older symbol-only name unchanged, next rule named", "/api/agent",
                   dict(bangs, name="!!!", provider="nope"), PROVIDER)
            relay.ok("/api/agent", dict(bangs, name=" !!! ", system_prompt="Changed."))
            expect(relay, "older symbol-only name cannot become other symbols",
                   "/api/agent", dict(bangs, name="@@@"), NAME_CHARS)
            relay.ok("/api/agent", {"action": "update", "slug": "old-twin",
                                    "name": "Quiet Hand", "system_prompt": "Changed."})
            major = {"action": "update", "slug": "old-major"}
            relay.ok("/api/agent", dict(major, name="Major", system_prompt="Changed."))
            relay.ok("/api/agent", dict(major, name=" Major "))
            expect(relay, "an older Major cannot move to a near name", "/api/agent",
                   dict(major, name="Major!"), LIKE.format("Major"))
            relay.ok("/api/agent", {"action": "update", "slug": "old-key",
                                    "name": "QuietHand", "system_prompt": "Changed."})
            check_names("older names save unchanged", relay,
                        {"old-bangs": "!!!", "old-twin": "Quiet Hand",
                         "old-major": "Major", "old-key": "QuietHand"})
            expect(relay, "an older shared name is not taken again", "/api/agent",
                   robot("Quiet Hand"), "A robot named Quiet Hand already exists.")
        finally:
            relay.stop()



def check_prompt_control_roundtrip(relay):
    """Controls in system_prompt must round-trip as valid JSON.

    Unescape decodes every HTTP JSON field; the session serializer must
    escape all controls below space so POST /api/agent and GET /api/session
    still parse. Names still refuse the same controls (checked earlier).
    """
    create = (
        b'{"name":"Prompt Ctrl",'
        b'"system_prompt":"Line\\tTwo\\rThree\\bX\\u001bY\\fZ",'
        b'"provider":"grok-build","save_pass":false}'
    )
    status, ctype, raw = relay.call("POST", "/api/agent", create)
    try:
        session = json.loads(raw)
    except json.JSONDecodeError as err:
        FAILURES.append(
            f"POST /api/agent with control prompt is not JSON: {err}; "
            f"HTTP {status} {raw[:160]!r}")
        return
    if status != 200 or not ctype.startswith("application/json"):
        FAILURES.append(
            f"create with control prompt: want 200 application/json; "
            f"got HTTP {status} {ctype} {raw[:160]!r}")
        return
    agents = [a for a in session.get("agents", [])
              if a.get("name") == "Prompt Ctrl"]
    if not agents:
        FAILURES.append("Prompt Ctrl missing from create session reply")
        return
    prompt = agents[0].get("prompt") or ""
    want_chars = (
        ("tab", "\t"),
        ("cr", "\r"),
        ("bs", "\b"),
        ("esc", "\x1b"),
        ("ff", "\f"),
    )
    missing = [label for label, ch in want_chars if ch not in prompt]
    if missing:
        FAILURES.append(
            f"create session prompt missing decoded controls {missing}; "
            f"got {prompt!r}")
    else:
        print("reasons: ok create session JSON with control prompt parses")

    status, ctype, raw = relay.call("GET", "/api/session")
    try:
        session = json.loads(raw)
    except json.JSONDecodeError as err:
        FAILURES.append(
            f"GET /api/session after control prompt is not JSON: {err}; "
            f"HTTP {status} {raw[:160]!r}")
        return
    if status != 200:
        FAILURES.append(f"GET /api/session after control prompt: HTTP {status}")
        return
    agents = [a for a in session.get("agents", [])
              if a.get("name") == "Prompt Ctrl"]
    if not agents:
        FAILURES.append("Prompt Ctrl missing from GET /api/session")
        return
    prompt = agents[0].get("prompt") or ""
    missing = [label for label, ch in want_chars if ch not in prompt]
    if missing:
        FAILURES.append(
            f"GET session prompt missing decoded controls {missing}; "
            f"got {prompt!r}")
    else:
        print("reasons: ok GET /api/session JSON with control prompt parses")

    # Edit the same robot with more escaped controls; reply must stay JSON.
    edit = (
        b'{"action":"update","slug":"prompt-ctrl",'
        b'"system_prompt":"Edit\\u0009A\\nB\\u001BY"}'
    )
    status, ctype, raw = relay.call("POST", "/api/agent", edit)
    try:
        session = json.loads(raw)
    except json.JSONDecodeError as err:
        FAILURES.append(
            f"edit with control prompt is not JSON: {err}; "
            f"HTTP {status} {raw[:160]!r}")
        return
    if status != 200:
        FAILURES.append(
            f"edit with control prompt: want 200; got HTTP {status} "
            f"{raw[:160]!r}")
        return
    agents = [a for a in session.get("agents", [])
              if a.get("slug") == "prompt-ctrl"]
    if not agents:
        FAILURES.append("prompt-ctrl missing after edit")
        return
    prompt = agents[0].get("prompt") or ""
    if "\t" not in prompt or "\n" not in prompt or "\x1b" not in prompt:
        FAILURES.append(f"edit session prompt missing controls; got {prompt!r}")
    else:
        print("reasons: ok edit session JSON with control prompt parses")
    relay.ok("/api/agent", {"action": "delete", "slug": "prompt-ctrl"})


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
    session = relay.ok("/api/agent", robot("Walkbot Three"))
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



FORGE_CHECK = "loadoutRefuseReason(equippedSkills, skillById(data.id))"
FORGE_HOLD = "if (reason) refused = reason;"
FORGE_PUSH = "else equippedSkills.push(data.id);"
FORGE_SAY = "if (refused) skillNotice(refused);"
# The cap uses the sentence the other equip paths already show. One sentence.
FORGE_CAP = "This robot has reached its skill capacity. Remove a skill first."


def check_forge_cap(label, text):
    """Create a skill uses the same capacity check and does not add a sentence."""
    needs = (FORGE_CHECK, FORGE_HOLD, FORGE_PUSH, FORGE_SAY, FORGE_CAP)
    missing = [need for need in needs if need not in text]
    if missing:
        FAILURES.append(f"{label} forge must use the existing capacity sentence; "
                        f"missing {missing}")
        return
    if "created but not equipped" in text:
        FAILURES.append(f"{label} must not invent a second forge sentence")
        return
    print(f"reasons: ok {label} forge uses the existing capacity sentence")


def check_ui(relay):
    demo = (ROOT / "demo" / "index.html").read_text()
    _, _, served = relay.call("GET", "/")
    served = served.decode("utf-8", "replace")
    for label, text in (("demo/index.html", demo), ("served UI", served)):
        for need in (UI_KEEPS_REASON, UI_SHOWS_REASON, UI_CLONE_REASON,
                     UI_DELETE_REASON, UI_MAJOR_REASON, UI_FAV_SAVE_REASON,
                     UI_FAV_DELETE_REASON, UI_NAME_RULE, UI_ERR_WRAP,
                     UI_RULE_SHOWN, UI_NAME_HELP, UI_HELP_SHOWN):
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
        hidden = text.count(UI_HELP_HIDDEN)
        if hidden == 2:
            print(f"reasons: ok {label} hides #agent-name-help for Major and locked robots")
        else:
            FAILURES.append(f"{label} must hide #agent-name-help in both name locks "
                            f"(Major, locked robot); found {hidden}")
        missing = [need for need in UI_DEEPSEEK if need not in text]
        if missing or "Deepseek" in text:
            FAILURES.append(f"{label} must spell DeepSeek API: missing {missing}, "
                            f"'Deepseek' x{text.count('Deepseek')}")
        else:
            print(f"reasons: ok {label} spells DeepSeek API ({len(UI_DEEPSEEK)} places)")
        problem = name_rule_placement(text)
        if problem:
            FAILURES.append(f"{label}: {problem}")
        else:
            print(f"reasons: ok {label} has #agent-name-rule in #agent-identity "
                  "after the name input")
        check_forge_cap(label, text)
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
            check_name_rule(relay)
            check_prompt_control_roundtrip(relay)
            check_context_size(relay)
            check_favorite_inputs(relay, owned)
            check_favorite_store(relay)
            check_robot_full(relay)
            check_ui(relay)
        finally:
            relay.stop()
    check_restart()
    if FAILURES:
        for line in FAILURES:
            print(f"reasons check failed: {line}", file=sys.stderr)
        sys.exit(1)
    print("refusal reasons ok")


if __name__ == "__main__":
    main()
