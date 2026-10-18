#!/bin/sh
# VOICE V1 (PLAN_VOICE P1.1, P1.2, P1.5): /api/status reports stt (bool),
# tts (array, chain order) and voices (int) on a throwaway hush-relay.
# PATH is a stub dir only, so piper / espeak-ng come from this harness.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
work="$(mktemp -d)"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$work/pass"
mkdir -p "$work/pass" "$work/empty" "$work/piper" "$work/espeak"
printf '#!/bin/sh\nexit 0\n' > "$work/piper/piper"
printf '#!/bin/sh\nexit 0\n' > "$work/espeak/espeak-ng"
chmod 0755 "$work/piper/piper" "$work/espeak/espeak-ng"
bin="$(pwd)/hush-relay"
pid=""
cleanup() {
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    rm -rf "$work"
}
trap cleanup EXIT
fail() { echo "voice check failed: $1" >&2; exit 1; }
system_whisper=0
if [ -x /usr/bin/whisper ] || [ -x /usr/local/bin/whisper ]; then system_whisper=1; fi

# status <label> <PATH> <HUSH_WHISPER or ""> -> prints /api/status JSON
status() {
    home="$work/home-$1"
    mkdir -p "$home/voices/piper" "$work/cfg-$1"
    : > "$home/voices/piper/en_US-amy-low.onnx"
    port=$(hush_free_port) || exit 1
    env -u HUSH_WHISPER PATH="$2" HUSH_HOME="$home" HUSH_CONFIG_DIR="$work/cfg-$1" \
        ${3:+HUSH_WHISPER=$3} "$bin" --no-open "$port" >"$work/log-$1" 2>&1 &
    pid=$!
    i=0
    while [ "$i" -lt 100 ]; do
        tok=$(cat "$home/session.token" 2>/dev/null || true)
        if command curl -sf -H "X-Hush-Token: $tok" "http://127.0.0.1:$port/api/status" \
            >"$work/st-$1" 2>/dev/null; then break; fi
        i=$((i + 1)); sleep 0.05
    done
    kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; pid=""
    [ -s "$work/st-$1" ] || fail "$1: relay never answered /api/status"
    cat "$work/st-$1"
}
# check <label> <json> <python boolean expression over st>
check() {
    printf '%s' "$2" | python3 -c '
import json, sys
st = json.load(sys.stdin)
ok = eval(sys.argv[1])
print(("PIN ok   " if ok else "PIN FAIL ") + sys.argv[2] + "  " +
      json.dumps({k: st.get(k) for k in ("whisper", "stt", "tts", "voices")}))
sys.exit(0 if ok else 1)' "$3" "$1" || fail "$1"
}
TYPES='type(st["stt"]) is bool and type(st["tts"]) is list and type(st["voices"]) is int'

st=$(status none "$work/empty" "")
check "P1.1 types" "$st" "$TYPES"
if [ "$system_whisper" -eq 0 ]; then
    check "P1.2 no whisper no piper -> stt:false,tts:[]" "$st" \
        'st["stt"] is False and st["tts"] == [] and st["voices"] == 0 and st["whisper"] is False'
else
    echo "SKIP P1.2 stt:false: a system whisper binary is installed"
fi
st=$(status piper "$work/piper" "")
check "P1.5 piper present -> tts:[piper], voices 1" "$st" \
    'st["tts"] == ["piper"] and st["voices"] == 1'
st=$(status espeak "$work/espeak" "")
check "P1.5 piper absent -> next engine espeak, voices 0" "$st" \
    'st["tts"] == ["espeak"] and st["voices"] == 0'
st=$(status whisper "$work/empty" 1)
check "STT whisper present (HUSH_WHISPER=1) -> stt true, whisper true" "$st" \
    'st["stt"] is True and st["whisper"] is True and st["tts"] == []'
# CoS 13:54 read-only pins over the source (no relay needed):
#   R1 hush_voice.c spawns nothing and writes nothing (detection only).
#   R2 the PWA voice block adds no ?k= URL, and its only getUserMedia call
#      asks for { audio: true } (mic-only, never video).
#   R3 (Ops #363) the voice CSS and UI code use theme tokens only (no hex
#      literal), no emoji, no innerHTML; every Test mic result code has a
#      plain-words line in VOICE_CHECK_TEXT, so no raw error name can show.
python3 - <<'PY' || fail "read-only source pins"
import re, sys
c = open("src/hush_voice.c").read()
c = re.sub(r"/\*.*?\*/", "", c, flags=re.S)
banned = r"\b(exec[lv]p?e?|fork|vfork|posix_spawnp?|popen|system|fopen|open|creat|mkdir|rename|unlink|write|fwrite|setenv|putenv)\s*\("
hits = re.findall(banned, c)
print(("PIN ok   " if not hits else "PIN FAIL ") + "R1 hush_voice.c no spawn/write calls  " + str(hits))
h = open("demo/index.html").read()
a = h.index("VOICE V1 capability probe (PLAN_VOICE")
b = h.index("function voiceCapsLines(", a)
block = h[a:b]
gum = re.findall(r"getUserMedia\(([^)]*)\)", block)
ok2 = "?k=" not in block and gum == ["{ audio: true }"]
print(("PIN ok   " if ok2 else "PIN FAIL ") + "R2 voice block: no ?k=, getUserMedia args " + str(gum))
css = h[h.index("/* VOICE V1: relay-drawer capability chip"):h.index("/* Mid widths: roster drops")]
ui = h[a:h.index("    function render(events, status) {", a)]
hexes = re.findall(r"#[0-9a-fA-F]{3,8}\b", re.sub(r"/\*.*?\*/", "", css + ui, flags=re.S))
emoji = [ch for ch in css + ui if ord(ch) >= 0x2190]
def obj_keys(name):
    body = re.search(r"const " + name + r" = \{(.*?)\n    \};", ui, re.S).group(1)
    return re.findall(r"^\s*(\w+):", body, re.M), re.findall(r':\s*"(\w+)"', body)
check_keys, _ = obj_keys("VOICE_CHECK_TEXT")
_, err_codes = obj_keys("VOICE_MIC_ERRORS")
codes = set(err_codes) | {"ok", "silent", "unsupported", "failed", "pending"}
missing = sorted(codes - set(check_keys))
ok3 = not hexes and not emoji and "innerHTML" not in ui and not missing and len(err_codes) >= 6
print(("PIN ok   " if ok3 else "PIN FAIL ") + "R3 voice UI: hex " + str(hexes) + " emoji " + str(emoji)
      + " codes without words " + str(missing))
sys.exit(0 if not hits and ok2 and ok3 else 1)
PY
echo "voice status ok"
