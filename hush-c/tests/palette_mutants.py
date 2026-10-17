#!/usr/bin/env python3
"""palette_mutants.py: mutation check for the field-office palette and mention pins (PR #325).

Each mutant makes one edit to demo/index.html, rebuilds hush-relay, and runs
tests/check_palette_tokens.py. It runs tests/check_palette.sh only when the
static check has not already failed on the named pin. A mutant counts as
CAUGHT only if a "check failed:" line contains its pin; "clean" (no edit)
must pass both. index.html is restored and hush-relay rebuilt on every exit.

Not part of make test (one browser run per mutant, about 45 s each).
usage: python3 tests/palette_mutants.py <outdir> [name-prefix ...]   (outdir outside the repo)
Env: MAKE (default gmake if present, else make); the check_palette.sh env
(HUSH_CHROME_BIN, HUSH_CDP_TIMEOUT_MS, ...) is passed through.
"""
import json, os, shutil, signal, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDX = os.path.join(R, "demo", "index.html")
if len(sys.argv) < 2:
    sys.exit("usage: python3 tests/palette_mutants.py <outdir> [name-prefix ...]")
OUT = os.path.abspath(sys.argv[1])
ONLY = sys.argv[2:]
MAKE = os.environ.get("MAKE") or ("gmake" if shutil.which("gmake") else "make")
FO = 'html[data-theme="field-office"]'

# (name, find, replace, pin). find must occur exactly once.
M = [
    ("clean", None, None, None),
    # Mentions (Gauge #325 r1/r2)
    ("N9 tick repaint call removed", "        syncApproval();\n        repaintPendingMentions();\n        whisperReady",
     "        syncApproval();\n        whisperReady", "K7 tick roster does not rename"),
    ("R1 roster guard removed", "      if (rosterKey === mentionRosterSeen) return;\n", "", "K7 tick re-renders"),
    ("R2 roster key frozen", "      mentionRosterSeen = rosterKey;\n",
     "      if (mentionRosterSeen === null) mentionRosterSeen = rosterKey;\n", "K7 tick re-renders"),
    ("R3 setSession repaint gone", "      syncApproval();\n      repaintPendingMentions();\n    }",
     "      syncApproval();\n    }", "K7 roster load"),
    ("T5 roster key is the roster size", '      const rosterKey = mentionRoster().map((h) => h.npub + "=" + h.name).sort().join("|");',
     "      const rosterKey = String(mentionRoster().length);", "K7 tick roster does not rename"),
    ("U3 aria-label dropped", '          span.setAttribute("aria-label", "unknown member " + npub.slice(0, 12));\n', "",
     "lacks the short npub"),
    ("U5 raw npub text node after the pill", "          pending = true;\n          container.appendChild(span);\n",
     "          pending = true;\n          container.appendChild(span);\n          container.appendChild(document.createTextNode(npub));\n",
     "the note body text paints an npub"),
    ("U7 lastIndex not advanced", "          container.appendChild(span);\n          last = re.lastIndex;\n          continue;",
     "          container.appendChild(span);\n          continue;", "the note body text paints an npub"),
    ("N21 --fo-inv-bad -> --surface", "  --fo-inv-bad: var(--accent);", "  --fo-inv-bad: var(--surface);", "K8b field-office"),
    # No white / call tile / skills (Ops #325 @3592ecaa)
    ("W1 tile ink token #fff", "  --fo-tile-ink: var(--fg);", "  --fo-tile-ink: #fff;", "S8 field-office token --fo-tile-ink"),
    ("W2 field-office rule paints #fff", FO + " .tile-mute { border: 1px solid var(--line); }",
     FO + " .tile-mute { border: 1px solid var(--line); color: #fff; }", "S1 "),
    ("W3 script sets white by name", '          mute.textContent = "Mute";',
     '          mute.textContent = "Mute";\n          mute.style.color = "white";', "W1 call-stage@1440 white color"),
    ("W4 near-white token", "  --fo-tile-label: var(--surface-2);", "  --fo-tile-label: #f4f0e6;", "S8 field-office token --fo-tile-label"),
    ("W5 unscoped white custom property", "</style>", ":root { --k-white-probe: #ffffff; }\n</style>", "S8 index.html"),
    ("W6 Muted slip fix removed", FO + " .tile-mute.on { color: var(--surface-2); border-color: var(--surface-2); }\n", "",
     "C1 call-stage@1440"),
    ("W7 tile label on ink (Ops 1.23:1)", "  --fo-tile-label: var(--surface-2);", "  --fo-tile-label: var(--fg);", "C1 call-stage@1440"),
    ("W8 lifetime ink #ffd75e", "  --fo-life-always: #4a380c;", "  --fo-life-always: #ffd75e;", "K10 tokens@1440"),
    ("W9 portrait rim #0ff", "  --fo-doll-rim: var(--line);", "  --fo-doll-rim: #0ff;", "K10 tokens@1440"),
    # Channel voice button (CoS 10:54)
    ("V1 voice rim fix reverted", "  --fo-voice-rim: var(--accent);        /* channel voice / robot call rim: --accent-dim was 2.89:1 on --surface */\n", "",
     "K11@1440 rest chan-voice edge"),
    ("V2 voice hover fix reverted", "  --fo-voice-hover-bg: var(--accent);   /* hover slip; ink on --accent-dim was 1.84:1 */\n  --fo-voice-hover-ink: var(--surface-2);\n", "",
     "K11@1440 hover chan-voice glyph"),
    # Shared literals behind --fo-* (fo-literal-tokens L set)
    ("L1 drop tok-kw token", "  --fo-tok-kw: #2a4466;                 /* Canvas syntax ink, >= 4.5:1 on --surface-2 */\n", "",
     "L1 field-office@1440 tok-kw|color"),
    ("L2 translucent tile label", "  --fo-tile-label: var(--surface-2);", "  --fo-tile-label: rgb(0 0 0 / 0.55);",
     "L1 field-office@1440 tile-span|background-color"),
    ("L3 lifetime ink too light", "  --fo-life-always: #4a380c;", "  --fo-life-always: #5a4410;", "L1 field-office@1440 life-always|color"),
    ("L4 fallback literal drifts", "var(--fo-lang-python, #3776ab)", "var(--fo-lang-python, #3776ac)", "L1 dark@1440 code-python"),
    ("L5 token leaks to every theme", "</style>", ":root { --fo-lang-go: #22605c; }\n</style>", "L1 dark@1440 code-go"),
    ("L6 bare #f66 back", ".skill-slot.drop-no { border-color: var(--fo-slot-no, #f66); }", ".skill-slot.drop-no { border-color: #f66; }", "S7 "),
    ("L7 bare literal back (overridden rule)", "box-shadow: inset 0 1px 3px var(--fo-shade, rgb(0 0 0 / 0.45));",
     "box-shadow: inset 0 1px 3px rgb(0 0 0 / 0.45);", "S7 "),
    ("L8 fallback names an undefined token", "var(--fo-tok-str, #86efac)", "var(--fo-tok-strx, #86efac)", "S7 "),
    ("L9 drop an override-only token", "  --fo-inv-drag: var(--accent);         /* inventory item being dragged */\n", "", "S7 "),
    ("L10 tile ink unreadable", "  --fo-tile-ink: var(--fg);", "  --fo-tile-ink: var(--accent-ink);", "L1 field-office@1440 tile-mute|color"),
    ("L11 code spine off-palette", "  --fo-lang-js: #5a4410;", "  --fo-lang-js: #f7df1e;", "L1 field-office@1440 code-js"),
    ("L12 bare #7dd3fc back", "#code-canvas-hi .tok-kw { color: var(--fo-tok-kw, #7dd3fc); }", "#code-canvas-hi .tok-kw { color: #7dd3fc; }", "S7 "),
    # Gauge #325 r5 (r6)
    ("P1 refreshPartials paints raw text", "              paintPartial(el, ans.text);", "              el.textContent = ans.text;", "K7p refreshPartials .partial paints an npub"),
    ("P2 paintThink paints raw text", "        if (text) paintPartial(prev, text);", "        if (text) prev.textContent = text;", "K7p paintThink .partial paints an npub"),
    # Gauge #325 r6 finding 1 (r7): the clear in paintPartial; without it each poll appends.
    ("P4 paintPartial clear removed", "    function paintPartial(el, text) {\n      el.textContent = \"\";\n      renderPreservingMentions(text, el);",
     "    function paintPartial(el, text) {\n      renderPreservingMentions(text, el);", "K7p refreshPartials .partial reads"),
    ("P6 refreshPartials skips the paint", "              paintPartial(el, ans.text);", "              void el;", "K7p refreshPartials .partial reads"),
    ("U13 text after the last mention dropped", 'if (last < (text || "").length) {', "if (false) {", "K7p paintThink .partial reads"),
    ("T8 roster key not sorted", '      const rosterKey = mentionRoster().map((h) => h.npub + "=" + h.name).sort().join("|");',
     '      const rosterKey = mentionRoster().map((h) => h.npub + "=" + h.name).join("|");', "K7k roster key is order-sensitive"),
    ("T9 roster key = names only", '      const rosterKey = mentionRoster().map((h) => h.npub + "=" + h.name).sort().join("|");',
     '      const rosterKey = mentionRoster().map((h) => h.name).sort().join("|");', "K7k roster key ignores the npub half"),
    ("T10 roster key = npubs only", '      const rosterKey = mentionRoster().map((h) => h.npub + "=" + h.name).sort().join("|");',
     '      const rosterKey = mentionRoster().map((h) => h.npub).sort().join("|");', "K7k roster key ignores the name half"),
    ("V7 voice hover-ink fallback drifts", "color: var(--fo-voice-hover-ink, var(--accent)); }", "color: var(--fo-voice-hover-ink, var(--fg)); }", "K11o "),
    ("V8 voice rim fallback drifts", "border-color: var(--fo-voice-rim, var(--accent-dim)); }", "border-color: var(--fo-voice-rim, var(--line)); }", "K11o "),
]


def run(cmd):
    return subprocess.run(cmd, cwd=R, capture_output=True, text=True)


def main():
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(143))  # still restores index.html
    os.makedirs(OUT, exist_ok=True)
    orig = open(IDX, encoding="utf-8").read()
    res = []
    try:
        for name, find, repl, pin in M:
            if ONLY and name != "clean" and not any(name.startswith(p) for p in ONLY):
                continue
            src = orig
            if find:
                assert src.count(find) == 1, (name, src.count(find))
                src = src.replace(find, repl, 1)
            open(IDX, "w", encoding="utf-8").write(src)
            subprocess.run([MAKE, "hush-relay"], cwd=R, check=True, capture_output=True)
            st = run(["python3", "tests/check_palette_tokens.py"])
            log = st.stdout + st.stderr
            static_hit = pin and any(pin in l for l in log.splitlines() if "check failed:" in l)
            rc = st.returncode
            if not static_hit:
                p = run(["sh", "tests/check_palette.sh"])
                log += p.stdout + p.stderr
                rc = rc or p.returncode
            open(os.path.join(OUT, name.split()[0] + ".log"), "w").write(log)
            fails = [l for l in log.splitlines() if "check failed:" in l]
            if pin is None:
                v = "CLEAN" if rc == 0 and "palette check passed" in log else "DIRTY"
            else:
                v = "CAUGHT" if rc and any(pin in l for l in fails) else "SURVIVED"
            first = [f for f in fails if pin and pin in f][:1]
            res.append({"mutant": name, "pin": pin, "verdict": v, "static_only": bool(static_hit), "first": first})
            print(name, v, first, flush=True)
    finally:
        open(IDX, "w", encoding="utf-8").write(orig)
        subprocess.run([MAKE, "hush-relay"], cwd=R, capture_output=True)
    json.dump(res, open(os.path.join(OUT, "mutants.json"), "w"), indent=1)
    bad = [r for r in res if r["verdict"] in ("SURVIVED", "DIRTY")]
    print(f"palette mutants: {sum(r['verdict'] == 'CAUGHT' for r in res)}/{sum(r['pin'] is not None for r in res)} caught, "
          f"clean {'passes' if any(r['verdict'] == 'CLEAN' for r in res) else 'NOT RUN/FAILS'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
