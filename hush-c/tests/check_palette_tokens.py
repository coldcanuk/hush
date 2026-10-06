#!/usr/bin/env python3
"""check_palette_tokens.py: static pins for the field-office palette (#282).

S1  No field-office rule paints with a literal colour. The palette lives in
    the html[data-theme="field-office"] token block only, so a hex, rgb(),
    hsl() or named colour in any other field-office rule fails.
S2  The token block carries the dossier-khaki values (a revert to the old
    manila tokens, or a token changed without updating this pin, fails).
S3  The old manila literals appear nowhere in the stylesheet outside
    comments.
S4  The ink-stamp message actions (.thread-btn, .note-files button,
    .think-stop) are styled under field-office and use tokens.
Reads demo/index.html only; the painted contrast is check_palette.sh.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HTML = os.path.join(HERE, "..", "demo", "index.html")

TOKENS = {
    "--bg": "#c2b185", "--surface": "#cdbd92", "--surface-2": "#d6c79d",
    "--line": "#4b4a2c", "--fg": "#1f1c12", "--muted": "#433f27",
    "--faint": "#4a452f", "--accent": "#7c241d", "--accent-ink": "#ddcea4",
    "--accent-dim": "#9a5a40", "--warn": "#7c241d",
}
OLD_MANILA = ["#d4c5a3", "#e2d5b8", "#ebdcb9", "#5c4a3d", "#2a241e", "#a3352c",
              "#7a6752", "#c86a5a", "#f4ead2", "#7a2720", "rgb(92 74 61",
              "rgba(42, 36, 30"]
LITERAL = re.compile(
    r"#[0-9a-fA-F]{3,8}\b|\b(?:rgb|rgba|hsl|hsla|oklch|oklab|lab|lch|hwb)\(|"
    r"(?<![-\w])(?:white|black|red|green|blue|gray|grey|silver|maroon|olive|"
    r"navy|teal|aqua|fuchsia|purple|yellow|orange|ivory|beige|wheat|tan)(?![-\w])",
    re.I)
TOKEN_SEL = re.compile(r'^html\[data-theme="field-office"\]$')

failures = []


def fail(msg):
    failures.append(msg)


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), text, flags=re.S)


def style_text(src):
    start = src.index("<style>")
    end = src.index("</style>", start)
    return start, strip_comments(src[start:end])


def line_of(src, base, pos):
    return src.count("\n", 0, base + pos) + 1


def rules(css):
    """Innermost rules: (selector, body, body offset)."""
    for m in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
        yield m.group(1).strip(), m.group(2), m.start(2)


def main():
    src = open(HTML, encoding="utf-8").read()
    base, css = style_text(src)
    token_bodies = []
    stamp = {".thread-btn": False, ".note-files button": False, ".think-stop": False}
    for sel, body, off in rules(css):
        if 'data-theme="field-office"' not in sel:
            continue
        if TOKEN_SEL.match(sel) and re.search(r"(?:^|[;\s])--[a-z0-9-]+\s*:", body):
            token_bodies.append(body)
            continue
        # Custom-property declarations may only live in the token block.
        for m in re.finditer(r"([a-zA-Z-]+)\s*:([^;]*)", body):
            prop, val = m.group(1), m.group(2)
            if prop.startswith("--"):
                fail(f"S1 index.html:{line_of(src, base, off + m.start())} custom property "
                     f"{prop} outside the token block: {sel[:80]}")
                continue
            if prop.lower() in ("content", "font", "font-family", "src"):
                continue
            hit = LITERAL.search(val)
            if hit:
                fail(f"S1 index.html:{line_of(src, base, off + m.start())} literal colour "
                     f"{hit.group(0)!r} in {prop} of {sel[:80]}")
        for key in stamp:
            if re.search(r"\[data-theme=\"field-office\"\][^,{]*" + re.escape(key) + r"\b", sel) and \
                    re.search(r"min-height\s*:\s*22px", body):
                stamp[key] = True
    if len(token_bodies) != 1:
        fail(f"S2 expected one field-office token block, found {len(token_bodies)}")
    else:
        decl = dict((k.strip(), v.strip().lower()) for k, v in
                    re.findall(r"(--[a-z0-9-]+)\s*:\s*([^;]+);", token_bodies[0]))
        for name, want in TOKENS.items():
            if decl.get(name) != want:
                fail(f"S2 token {name} is {decl.get(name)!r}, want {want}")
    # The header lamp (all themes, <= 480px) keeps its own dark rim.
    low = re.sub(r"html\[data-theme\] header #badge \{[^}]*\}", lambda m: " " * len(m.group(0)), css).lower()
    for lit in OLD_MANILA:
        if lit in low:
            fail(f"S3 old manila literal {lit} still in the stylesheet "
                 f"(index.html:{line_of(src, base, low.index(lit))})")
    for key, ok in stamp.items():
        if not ok:
            fail(f"S4 no field-office ink-stamp rule (min-height: 22px) for {key}")
    if failures:
        for f in failures:
            print("palette tokens check failed: " + f, file=sys.stderr)
        return 1
    print("palette tokens check passed: one token block, %d tokens pinned, "
          "no literal colour in field-office rules, ink-stamp family present" % len(TOKENS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
