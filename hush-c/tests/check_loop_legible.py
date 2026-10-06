#!/usr/bin/env python3
"""#280 / #279 write-legible-c pin: the loop's functions (and the follow_kick
path they touch), the approval gate and setting (#279), and every function
in the loop's and the approval's C tests stay at most 40 lines, signature to
closing brace, with at most four parameters (c-standard.md caps;
write-legible-c applies them to test code written in C too).
Definitions are read with the return type on the name line or alone on the
line above, and with "{" on its own line or ending the signature. A built-in
self-test proves both layouts, each cap, the staleness guard, and that
MIN_SEEN for each C test equals its real function count."""
import re
import sys

LINE_CAP = 40
PARAM_CAP = 4
FILES = {
    "src/agent_loop.c": None,
    "src/agent_dispatch.c": re.compile(
        r"hush_agent_(follow_kick|follow_wave|loop_\w+|chaperon_say|turn_cap|turns_full"
        r"|begin_work|begin_approved|follow_release|reset_follow|is_work_note)$"),
    "src/agent_prompt.c": re.compile(r"hush_agent_append_loop_lead$"),
    "src/agent_approve.c": None,
    "src/hush_roster.c": re.compile(r"hush_roster_(approval_parse|approval_id|format_profile)$"),
    "src/hush_launch.c": re.compile(
        r"hush_launch_(set_approval|take_approval|take_roster|put_roster)$"),
    "src/api_identity.c": re.compile(r"hush_http_serve_profile$"),
    "tests/test_loop.c": None,
    "tests/test_approve.c": None,
}
# Functions in tests/test_loop.c today. The scan must still find all of
# them, so no test function can drop out of the caps unnoticed.
TEST_LOOP_FUNCTIONS = 18
# Functions in tests/test_approve.c today (#279), held to the same rule.
TEST_APPROVE_FUNCTIONS = 24
TEST_COUNTS = {
    "tests/test_loop.c": TEST_LOOP_FUNCTIONS,
    "tests/test_approve.c": TEST_APPROVE_FUNCTIONS,
}
# Each scanned file must still yield at least this many functions (today's
# counts), so a rename, move or new layout cannot silently shrink the scan.
MIN_SEEN = {
    "src/agent_loop.c": 9,
    "src/agent_dispatch.c": 18,
    "src/agent_prompt.c": 1,
    "src/agent_approve.c": 12,
    "src/hush_roster.c": 3,
    "src/hush_launch.c": 4,
    "src/api_identity.c": 1,
    "tests/test_loop.c": TEST_LOOP_FUNCTIONS,
    "tests/test_approve.c": TEST_APPROVE_FUNCTIONS,
}
# A definition's name line: optional storage class and return type, then
# "name(". The return type may also sit alone on the line above (K&R).
SIG = re.compile(r"^(?:static\s+)?(?:[A-Za-z_][\w\s\*]*?[\s\*])?([A-Za-z_]\w*)\(")
# A line holding only a return type, e.g. "static void" or "const char *".
TYPE_LINE = re.compile(r"^(?:static\s+)?[A-Za-z_][\w\s\*]*$")


def signature_end(lines, i):
    """Index of the line that closes the parameter list opened on line i."""
    depth = 0
    for j in range(i, len(lines)):
        depth += lines[j].count("(") - lines[j].count(")")
        if depth <= 0:
            return j
    return len(lines) - 1


def body_open(lines, j):
    """Index of the line holding the body's "{", or -1 for a prototype."""
    tail = lines[j].rstrip()
    if tail.endswith("{"):
        return j
    if tail.endswith(")") and j + 1 < len(lines) and lines[j + 1].strip() == "{":
        return j + 1
    return -1


def functions(path):
    """Yields (name, first_line, line_count, params) for each definition.
    Handles the return type on the name line or alone on the line above,
    and the "{" on its own line or at the end of the signature."""
    lines = open(path).read().split("\n")
    i = 0
    while i < len(lines):
        m = SIG.match(lines[i])
        if not m:
            i += 1
            continue
        j = signature_end(lines, i)
        brace = body_open(lines, j)
        if brace < 0:
            i = j + 1
            continue
        first = i - 1 if i > 0 and TYPE_LINE.match(lines[i - 1]) else i
        sig = " ".join(lines[i:j + 1])
        args = sig[sig.index("(") + 1:sig.rindex(")")].strip()
        params = 0 if args in ("", "void") else args.count(",") + 1
        k = brace + 1
        while k < len(lines) and lines[k].rstrip() != "}":
            k += 1
        yield m.group(1), first + 1, k - first + 1, params
        i = k + 1


def scan(files, min_seen):
    """Returns one LEGIBLE complaint per cap break or stale file."""
    bad = []
    for path, pattern in files.items():
        seen = 0
        for name, at, count, params in functions(path):
            if pattern is not None and not pattern.search(name):
                continue
            seen += 1
            if count > LINE_CAP:
                bad.append(f"{path}:{at} {name} is {count} lines (cap {LINE_CAP})")
            if params > PARAM_CAP:
                bad.append(f"{path}:{at} {name} takes {params} params (cap {PARAM_CAP})")
        if seen < min_seen[path]:
            bad.append(f"{path}: only {seen} functions matched; the pattern list is stale")
    return bad


# Self-test fixtures: each layout the scan must read. PAD is one body line.
# The two long cases are exactly LINE_CAP + 1 lines, counting the K&R
# return-type line, so dropping any counted line hides the break.
PAD = "    (void)0;\n"
SELF_CASES = {
    "kr_long": ("static void\nkr_long(void)\n{\n" + PAD * (LINE_CAP - 3) + "}\n", 1, 1),
    "brace_long": ("static void brace_long(void) {\n" + PAD * (LINE_CAP - 1) + "}\n", 1, 1),
    "kr_params": ("static int\nkr_params(int a, int b, int c, int d, int e)\n{\n"
                  "    return a + b + c + d + e;\n}\n", 1, 1),
    "brace_params": ("static int brace_params(int a, int b, int c, int d,\n"
                     "                        int e) {\n    return a + b + c + d + e;\n}\n",
                     1, 1),
    "short_both": ("static int\nkr_ok(int a)\n{\n    return a;\n}\n\n"
                   "static int brace_ok(int a) {\n    return a;\n}\n", 2, 0),
}


def self_test():
    """Proves the scan reads both layouts, flags each cap, keeps the
    staleness guard, and that MIN_SEEN has no slack for either C test."""
    import os
    import tempfile
    errors = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, (text, want_seen, want_bad) in SELF_CASES.items():
            path = os.path.join(tmp, name + ".c")
            with open(path, "w") as out:
                out.write(text)
            seen = sum(1 for _ in functions(path))
            bad = scan({path: None}, {path: want_seen})
            if seen != want_seen or len(bad) != want_bad:
                errors.append(f"{name}: seen {seen} (want {want_seen}), flags {bad}")
            if not any("stale" in b for b in scan({path: None}, {path: want_seen + 1})):
                errors.append(f"{name}: staleness guard did not fire")
    for path, named in TEST_COUNTS.items():
        actual = sum(1 for _ in functions(path))
        if MIN_SEEN[path] != actual or named != actual:
            errors.append(f"{path.split('/')[-1]} has {actual} functions; MIN_SEEN must match it")
    return ["self-test " + line for line in errors]


def main():
    bad = self_test() + scan(FILES, MIN_SEEN)
    for line in bad:
        print("LEGIBLE " + line)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
