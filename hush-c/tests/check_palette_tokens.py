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
S5  No inline style= attribute in the markup carries a literal colour
    (an inline style beats every field-office rule).
S6  No JavaScript string literal carries a hex / rgb() / hsl() colour,
    except as the fallback of var(--fo-*, ...) for a token the field-office
    block defines (inline styles set from script beat the theme too).
S7  Rules not scoped to a theme (they paint in field-office as well) add no
    literal colour: a literal there is allowed only as the fallback of a
    defined --fo-* token (the other themes never define --fo-*, so they keep
    the literal). BASELINE_S7 is empty: main 900a486d had 63 such
    declarations, all now tokenized; a new one, or one with !important, fails. Rules scoped to another theme, or to
    html:not([data-theme="field-office"]), are not field-office paint.
S8  No white or near-white literal reaches field-office (Ops #325 "NO white
    in FO"): every literal in the field-office token block, and every literal
    in a custom property set by an unscoped rule that the token block does not
    redefine (field-office inherits it), has relative luminance < 0.80 and is
    not white / ivory / beige. S1, S5, S6 and S7 already forbid bare literals
    in field-office rules, inline styles, script strings and unscoped rules.
(Ops back-LOOK / Gauge G291-2.)
Reads demo/index.html only; the painted contrast is check_palette.sh.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HTML = os.environ.get("PALETTE_HTML") or os.path.join(HERE, "..", "demo", "index.html")

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
JS_COLOUR = re.compile(r"#[0-9a-fA-F]{3,8}\b|\b(?:rgb|rgba|hsl|hsla)\(")
FO_FALLBACK = re.compile(r"var\(\s*(--fo-[a-z0-9-]+)\s*,[^()]*(?:\([^()]*\)[^()]*)*\)")
COLOUR_PROPS = re.compile(r"(?:^|-)(?:color|background|border|outline|shadow|fill|stroke|caret|decoration)", re.I)
# (selector, property) of unscoped literal-colour declarations still allowed.
# main 900a486d had 63 declarations on 62 pairs; the follow-up to G291-2 put
# every one behind a var(--fo-*, <literal>) fallback, so the list is empty and
# any unscoped literal colour fails. Do not add to this list.
BASELINE_S7 = set()
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


def without_fo_fallbacks(val, defined):
    """Drop var(--fo-x, fallback) where --fo-x is a field-office token."""
    return FO_FALLBACK.sub(lambda m: "" if m.group(1) in defined else m.group(0), val)


def theme_scoped(sel):
    """Every selector in the list names a non-field-office theme."""
    parts = [x.strip() for x in sel.split(",")]
    return all(re.search(r'data-theme="(?!field-office)[^"]+"', x) or
               ':not([data-theme="field-office"])' in x for x in parts)


def scan_unscoped(src, base, css, defined, fallbacks):
    seen = 0
    for sel, body, off in rules(css):
        if 'data-theme="field-office"' in sel or theme_scoped(sel) or sel.lstrip().startswith("@"):
            continue
        for m in re.finditer(r"([a-zA-Z-]+)\s*:([^;]*)", body):
            prop, val = m.group(1).lower(), m.group(2)
            if prop.startswith("--") or prop in ("content", "font", "font-family", "src"):
                continue
            hit = LITERAL.search(without_fo_fallbacks(val, defined))
            if not hit:
                if any(f.group(1) in defined and LITERAL.search(f.group(0)) for f in FO_FALLBACK.finditer(val)):
                    fallbacks[0] += 1
                continue
            where = f"index.html:{line_of(src, base, off + m.start())}"
            key = (" ".join(sel.split()), prop)
            if "!important" in val:
                fail(f"S7 {where} literal colour {hit.group(0)!r} with !important in {prop} of {sel[:80]}")
            elif key not in BASELINE_S7:
                fail(f"S7 {where} literal colour {hit.group(0)!r} in {prop} of unscoped {sel[:80]} "
                     "(paints in field-office; use var(--fo-<token>, <literal>))")
            else:
                seen += 1
    return seen


NEAR_WHITE_NAMES = {"white", "ivory", "beige"}


def luminance(lit):
    """Relative luminance of a hex or rgb()/rgba() literal, else None."""
    lit = lit.strip().lower()
    m = re.fullmatch(r"#([0-9a-f]{3,8})", lit)
    if m:
        h = m.group(1)
        if len(h) in (3, 4):
            h = "".join(c * 2 for c in h[:3])
        rgb = [int(h[i:i + 2], 16) for i in (0, 2, 4)]
    else:
        m = re.match(r"rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)", lit)
        if not m:
            return None
        rgb = [float(x) for x in m.groups()]

    def lin(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(c) for c in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


NEAR_WHITE_LIT = re.compile(r"#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\)|\b[a-zA-Z]+\b")


def near_white(val):
    """First white or near-white literal in a declaration value, else None."""
    for m in NEAR_WHITE_LIT.finditer(val):
        tok = m.group(0)
        if tok.lower() in NEAR_WHITE_NAMES:
            return tok
        if tok.startswith("#") or tok.lower().startswith("rgb"):
            y = luminance(tok)
            if y is not None and y >= 0.80:
                return tok
    return None


def scan_near_white(src, base, css, token_body):
    """S8. Returns (token literals checked, inherited custom properties checked)."""
    fo_props = set(re.findall(r"(--[a-z0-9-]+)\s*:", token_body or ""))
    n_tok = 0
    for m in re.finditer(r"(--[a-z0-9-]+)\s*:\s*([^;]+);", token_body or ""):
        if not LITERAL.search(m.group(2)):
            continue
        n_tok += 1
        hit = near_white(m.group(2))
        if hit:
            fail(f"S8 field-office token {m.group(1)} is near-white {hit!r}")
    n_inh = 0
    for sel, body, off in rules(css):
        if 'data-theme="field-office"' in sel or theme_scoped(sel) or sel.lstrip().startswith("@"):
            continue
        for m in re.finditer(r"(--[a-z0-9-]+)\s*:([^;]*)", body):
            if m.group(1) in fo_props or not LITERAL.search(m.group(2)):
                continue
            n_inh += 1
            hit = near_white(m.group(2))
            if hit:
                fail(f"S8 index.html:{line_of(src, base, off + m.start())} {m.group(1)} = {hit!r} "
                     f"in unscoped {sel[:60]} reaches field-office (not redefined in its token block)")
    return n_tok, n_inh


def scan_inline_and_js(src, defined):
    s0 = src.index("</style>")
    sc = src.index("<script>", s0)
    markup = src[s0:sc]
    n_inline = 0
    for m in re.finditer(r'\sstyle\s*=\s*"([^"]*)"', markup):
        n_inline += 1
        hit = LITERAL.search(without_fo_fallbacks(m.group(1), defined))
        if hit:
            fail(f"S5 index.html:{src.count(chr(10), 0, s0 + m.start()) + 1} inline style literal colour "
                 f"{hit.group(0)!r}: {m.group(1)[:60]}")
    script = src[sc:src.rindex("</script>")]
    n_js = 0
    for m in re.finditer(r"(['\"`])((?:\\.|(?!\1).)*?)\1", script):
        body = m.group(2)
        if not JS_COLOUR.search(body):
            continue
        n_js += 1
        hit = JS_COLOUR.search(without_fo_fallbacks(body, defined))
        if hit:
            fail(f"S6 index.html:{src.count(chr(10), 0, sc + m.start()) + 1} script colour literal "
                 f"{hit.group(0)!r} in {m.group(0)[:60]}")
        # inline style="..." written by script markup strings
        for st in re.finditer(r'style=\\?"([^"\\]*)', body):
            h2 = LITERAL.search(without_fo_fallbacks(st.group(1), defined))
            if h2:
                fail(f"S5 index.html:{src.count(chr(10), 0, sc + m.start()) + 1} script markup inline style literal {h2.group(0)!r}")
    return n_inline, n_js


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
    defined = set()
    if token_bodies:
        defined = set(re.findall(r"(--fo-[a-z0-9-]+)\s*:", token_bodies[0]))
    fallbacks = [0]
    n_s7 = scan_unscoped(src, base, css, defined, fallbacks)
    n_inline, n_js = scan_inline_and_js(src, defined)
    n_tok, n_inh = scan_near_white(src, base, css, token_bodies[0] if token_bodies else "")
    if failures:
        for f in failures:
            print("palette tokens check failed: " + f, file=sys.stderr)
        return 1
    print("palette tokens check passed: one token block, %d tokens pinned, "
          "no literal colour in field-office rules, ink-stamp family present, "
          "%d markup inline styles and %d script colour strings without a bare literal, "
          "%d unscoped declarations with a literal only as a --fo-* fallback, "
          "%d bare unscoped literal declarations (%d baselined pairs allowed), none !important; "
          "S8 %d field-office token literals and %d inherited custom-property literals, none near-white"
          % (len(TOKENS), n_inline, n_js, fallbacks[0], n_s7, len(BASELINE_S7), n_tok, n_inh))
    return 0


if __name__ == "__main__":
    sys.exit(main())
