#!/bin/sh
# Embed demo UI assets as C string/byte constants (hush_ui_html.h).
# Usage: embed-ui.sh DEMO_DIR   (or embed-ui.sh FILE for the old HTML-only path)
set -eu
if [ "$#" -lt 1 ]; then
    echo "usage: embed-ui.sh DEMO_DIR" >&2
    exit 1
fi
src="$1"
if [ -d "$src" ]; then
    demo="$src"
else
    demo=$(dirname "$src")
fi
python3 - "$demo" << 'PY'
import sys
from pathlib import Path

demo = Path(sys.argv[1])

def c_string(name, text):
    lines = ["static const char %s[] =" % name]
    if not text.endswith("\n"):
        text += "\n"
    for line in text.splitlines():
        esc = line.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('"%s\\n"' % esc)
    lines.append(";")
    return "\n".join(lines)

def c_bytes(name, data):
    parts = ["static const unsigned char %s[] = {" % name]
    chunk = []
    for i, b in enumerate(data):
        chunk.append("0x%02x" % b)
        if len(chunk) == 12:
            parts.append("    " + ", ".join(chunk) + ",")
            chunk = []
    if chunk:
        parts.append("    " + ", ".join(chunk))
    parts.append("};")
    parts.append("enum { %s_LEN = %d };" % (name, len(data)))
    return "\n".join(parts)

html = (demo / "index.html").read_text(encoding="utf-8")
manifest = (demo / "manifest.webmanifest").read_text(encoding="utf-8")
sw = (demo / "sw.js").read_text(encoding="utf-8")
i192 = (demo / "icons" / "icon-192.png").read_bytes()
i512 = (demo / "icons" / "icon-512.png").read_bytes()
i180 = (demo / "icons" / "apple-touch-icon.png").read_bytes()

out = []
out.append("/* generated from demo/ — do not edit */")
out.append("#ifndef HUSH_UI_HTML_H")
out.append("#define HUSH_UI_HTML_H")
out.append("")
out.append(c_string("HUSH_UI_HTML", html))
out.append("")
out.append(c_string("HUSH_UI_MANIFEST", manifest))
out.append("")
out.append(c_string("HUSH_UI_SW", sw))
out.append("")
out.append(c_bytes("HUSH_UI_ICON_192", i192))
out.append("")
out.append(c_bytes("HUSH_UI_ICON_512", i512))
out.append("")
out.append(c_bytes("HUSH_UI_ICON_180", i180))
out.append("")
out.append("#endif /* HUSH_UI_HTML_H */")
sys.stdout.write("\n".join(out) + "\n")
PY
