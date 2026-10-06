#!/usr/bin/env python3
"""#280 write-legible-c pin: the loop's functions (and the follow_kick path
they touch) stay at most 40 lines, signature to closing brace, with at most
four parameters (c-standard.md caps)."""
import re
import sys

LINE_CAP = 40
PARAM_CAP = 4
FILES = {
    "src/agent_loop.c": None,
    "src/agent_dispatch.c": re.compile(
        r"hush_agent_(follow_kick|follow_wave|loop_\w+|chaperon_say|turn_cap|turns_full)$"),
    "src/agent_prompt.c": re.compile(r"hush_agent_append_loop_lead$"),
}
SIG = re.compile(r"^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b(hush_agent_\w+)\(")


def functions(path):
    """Yields (name, first_line, line_count, params) for each definition."""
    lines = open(path).read().split("\n")
    i = 0
    while i < len(lines):
        m = SIG.match(lines[i])
        if not m:
            i += 1
            continue
        j = i
        while j < len(lines) and not lines[j].rstrip().endswith((")", "{", ";")):
            j += 1
        if j + 1 >= len(lines) or lines[j].rstrip().endswith(";") or lines[j + 1] != "{":
            i = j + 1
            continue
        sig = " ".join(lines[i:j + 1])
        args = sig[sig.index("(") + 1:sig.rindex(")")].strip()
        params = 0 if args in ("", "void") else args.count(",") + 1
        k = j + 1
        while k < len(lines) and lines[k] != "}":
            k += 1
        yield m.group(1), i + 1, k - i + 1, params
        i = k + 1


def main():
    bad = []
    seen = 0
    for path, pattern in FILES.items():
        for name, at, count, params in functions(path):
            if pattern is not None and not pattern.search(name):
                continue
            seen += 1
            if count > LINE_CAP:
                bad.append(f"{path}:{at} {name} is {count} lines (cap {LINE_CAP})")
            if params > PARAM_CAP:
                bad.append(f"{path}:{at} {name} takes {params} params (cap {PARAM_CAP})")
    if seen < 20:
        bad.append(f"only {seen} functions matched; the pattern list is stale")
    for line in bad:
        print("LEGIBLE " + line)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
