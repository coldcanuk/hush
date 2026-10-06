#!/bin/sh
# Approval setting (#279): "Auto-approve" (default, today's behaviour) or
# "Approve every action" (the owner answers Yes or No in the thread before
# each robot turn). Pins the /api/profile round-trip and vibe.json restart,
# the single gate in hush_agent_begin_work on every turn path (first turn,
# follow wave, loop turn, election/plan), decline, void, and cap counting.
# A scripted fake grok plays the robots and counts its own starts.
set -eu
cd "$(dirname "$0")/.."
. ./tests/hush_free_port.sh
curl() { command curl -H "X-Hush-Token: $(cat "${HUSH_HOME:-$HOME/.hush}/session.token" 2>/dev/null || true)" "$@"; }

bin=./hush-relay
port=$(hush_free_port) || exit 1
log=$(mktemp)
home=$(mktemp -d)
pid=""

cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
    rm -rf "$home"
}
trap cleanup EXIT

fail() { echo "approve check failed: $1" >&2; exit 1; }

python3 tests/check_loop_legible.py || fail "approval functions break the 40-line / 4-param caps"

ASK_HAPPY="Approval needed: Happy wants to take a turn. Reply Yes or No in this thread."
NO_HAPPY="Turn declined: Happy stood down."
# The UI control posts the field the server reads, and mirrors what it saves.
grep -q 'name="approval" value="auto_approve" checked> Auto-approve' demo/index.html \
    || fail "Settings must offer Auto-approve (default checked)"
grep -q 'name="approval" value="approve_every_action"> Approve every action' demo/index.html \
    || fail "Settings must offer Approve every action"
grep -q 'api("/api/profile", { approval_mode: el.value })' demo/index.html \
    || fail "the Settings radio must post approval_mode"
grep -q 'session.approval_mode === "approve_every_action"' demo/index.html \
    || fail "the Settings radio must read approval_mode back from the session"

export HOME="$home"
export HUSH_HOME="$home/.hush"
export HUSH_CONFIG_DIR="$home/.config/hush"
export HUSH_PASS_HELPER="$(pwd)/tests/fake-pass.sh"
export HUSH_FAKE_PASS_DIR="$home/pass"
unset XDG_CONFIG_HOME
plan="$HUSH_CONFIG_DIR/loop"
mkdir -p "$home/bin" "$home/.grok" "$HUSH_CONFIG_DIR" "$HUSH_HOME" "$plan" "$home/pass"
# The fake grok plays Happy (lead), the election committee (elect), the
# elected leader's plan pass (plan) or anyone else (partner) from the system
# prompt, counts its starts in <who>.n, and prints line n of <who>.plan after
# its nth reply ("-" or no file prints nothing). The election pass prints only
# its plan line (the leader's name).
cat > "$home/bin/grok" <<'GROK'
#!/bin/sh
log="${HUSH_CONFIG_DIR}/grok-p.log"
dir="${HUSH_CONFIG_DIR}/loop"
prev=""
sys=""
for a in "$@"; do
    if [ "$prev" = "-p" ]; then printf 'P:%s\n' "$a" >> "$log"; fi
    if [ "$prev" = "--system-prompt-override" ]; then
        printf 'S:%s\n' "$a" >> "$log"
        sys="$a"
    fi
    prev="$a"
done
case "$sys" in
    *"You are Happy."*) who=lead ;;
    *) who=partner ;;
esac
case "$sys" in
    *"You are the election committee."*) who=elect ;;
    *"You are the leader. Organize the other robots"*) who=plan ;;
esac
n=$(cat "$dir/$who.n" 2>/dev/null || echo 0)
n=$((n + 1))
echo "$n" > "$dir/$who.n"
[ "$who" = elect ] || printf '%s turn %s.\n' "$who" "$n"
line=$(sed -n "${n}p" "$dir/$who.plan" 2>/dev/null || true)
if [ -n "$line" ] && [ "$line" != "-" ]; then printf '%s\n' "$line"; fi
GROK
chmod 0755 "$home/bin/grok"
PATH="$home/bin:$PATH"
export PATH
printf '%s\n' '{"ok":true}' > "$home/.grok/auth.json"

cat > "$home/thread.py" <<'PY'
import json, sys
events = json.load(open(sys.argv[1])).get("events") or []
cevents = json.load(open(sys.argv[2])).get("events") or []
marker, field = sys.argv[3], sys.argv[4]
root = ""
for e in events:
    if marker in (e.get("content") or "") and not e.get("reply_to"):
        root = e.get("id") or ""
counts = {"root": root, "lead": 0, "plan": 0, "partner": 0, "approval": 0, "declined": 0,
          "ask": 0, "stopped": 0, "chaperon": 0, "job_start": 0,
          "ask_happy": 0, "no_happy": 0, "elect_ask": 0, "startfail": 0}
for e in events:
    if not root or (e.get("reply_to") or "") != root:
        continue
    c = e.get("content") or ""
    if "lead turn" in c:
        counts["lead"] += 1
    if "partner turn" in c:
        counts["partner"] += 1
    if "plan turn" in c:
        counts["plan"] += 1
    if c.startswith("Approval needed: "):
        counts["approval"] += 1
    if c == "Approval needed: Happy wants to take a turn. Reply Yes or No in this thread.":
        counts["ask_happy"] += 1
    if c == "Approval needed: Happy wants to run the leader election. Reply Yes or No in this thread.":
        counts["elect_ask"] += 1
    if "could not start a turn" in c:
        counts["startfail"] += 1
    if c.startswith("Turn declined: "):
        counts["declined"] += 1
    if c == "Turn declined: Happy stood down.":
        counts["no_happy"] += 1
    if c == "Continue this loop? Reply Yes or No in this thread.":
        counts["ask"] += 1
    if c == "Loop stopped.":
        counts["stopped"] += 1
    if c.startswith("That's enough robot talk."):
        counts["chaperon"] += 1
for ce in cevents:
    if root and ce.get("type") == "job_start" and ce.get("root") == root:
        counts["job_start"] += 1
print(counts[field])
PY

start_relay() {
    "$bin" --no-open "$port" >"$log" 2>&1 &
    pid=$!
    i=0
    until curl -sf "http://127.0.0.1:${port}/api/session" >/dev/null 2>&1; do
        i=$((i + 1))
        [ "$i" -lt 100 ] || fail "relay did not start"
        sleep 0.05
    done
}
stop_relay() {
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    pid=""
}
session() { curl -sf "http://127.0.0.1:${port}/api/session"; }
mode() { session | sed -n 's/.*"approval_mode":"\([^"]*\)".*/\1/p'; }
profile() {
    curl -s -o "$home/profile.out" -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/profile" \
        -H 'Content-Type: application/json' -d "$1"
}

start_relay
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' -d '{"action":"create"}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/identity" \
    -H 'Content-Type: application/json' \
    -d '{"action":"ack_backup","save_pass":true}' >/dev/null
curl -sf -X POST "http://127.0.0.1:${port}/api/vibe" \
    -H 'Content-Type: application/json' -d '{"name":"HQ","about":"approve"}' >/dev/null
raise() {
    curl -sf -X POST "http://127.0.0.1:${port}/api/agent" \
        -H 'Content-Type: application/json' \
        -d "{\"name\":\"$1\",\"system_prompt\":\"Play riddles.\",\"provider\":\"grok-build\",\"save_pass\":true}"
}
npub_of() { printf '%s' "$1" | sed -n "s/.*\"slug\":\"$2\"[^}]*\"npub\":\"\([^\"]*\)\".*/\1/p"; }
happy=$(npub_of "$(raise Happy)" happy)
scout=$(npub_of "$(raise Scout)" scout)
builder=$(npub_of "$(raise Builder)" builder)
test -n "$happy" || fail "Happy not raised"
test -n "$scout" || fail "Scout not raised"
test -n "$builder" || fail "Builder not raised"

set_cap() {
    curl -sf -X POST "http://127.0.0.1:${port}/api/channel" \
        -H 'Content-Type: application/json' \
        -d "{\"action\":\"manage\",\"slug\":\"general\",\"max_robot_turns\":$1,\"burst_ms\":500,\"cooldown_s\":0}" \
        | grep -q "\"max_robot_turns\":$1" || fail "cap $1 not set"
}
fresh() {
    rm -f "$plan"/*.n
    printf '%s\n' $1 | tr '_' ' ' > "$plan/lead.plan"
    printf '%s\n' $2 | tr '_' ' ' > "$plan/partner.plan"
    printf '%s\n' ${3:-Happy} > "$plan/elect.plan"
}
starts() { cat "$plan/$1.n" 2>/dev/null || echo 0; }
# note NAME TEXT MENTIONS...: a top-level owner note tagged tag-NAME.
note() {
    name=$1
    text=$2
    shift 2
    extra=""
    k=0
    for m in "$@"; do
        extra="$extra,\"mention_$k\":\"$m\""
        k=$((k + 1))
    done
    curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
        -H 'Content-Type: application/json' \
        -d "{\"content\":\"$text tag-$name\",\"kind\":1,\"channel\":\"general\"$extra}" \
        | grep -q '"ok":true' || fail "$name note not stored"
}
field() {
    curl -sf "http://127.0.0.1:${port}/api/events" > "$home/ev.json"
    curl -sf "http://127.0.0.1:${port}/api/chan-events" > "$home/ce.json"
    python3 "$home/thread.py" "$home/ev.json" "$home/ce.json" "tag-$1" "$2"
}
wait_eq() {
    i=0
    while [ "$i" -lt 300 ]; do
        [ "$(field "$1" "$2")" = "$3" ] && return 0
        i=$((i + 1))
        sleep 0.05
    done
    fail "$1: $2 never reached $3 (got $(field "$1" "$2"))"
}
expect() {
    got=$(field "$1" "$2")
    [ "$got" = "$3" ] || fail "$1: $4 ($2=$got, want $3)"
}
expect_starts() {
    got=$(starts "$1")
    [ "$got" = "$2" ] || fail "$3 ($1 program starts=$got, want $2)"
}
settle() { sleep 1.5; }
reply() {
    root=$(field "$1" root)
    curl -sf -X POST "http://127.0.0.1:${port}/api/event" \
        -H 'Content-Type: application/json' \
        -d "{\"content\":\"$2\",\"kind\":1,\"channel\":\"general\",\"reply_to\":\"$root\"}" \
        | grep -q '"ok":true' || fail "$1 reply not stored"
}
set_cap 8

# A1: a fresh vibe auto-approves; a mention runs at once with no approval line.
[ "$(mode)" = auto_approve ] || fail "A1 a fresh vibe must default to auto_approve (got '$(mode)')"
fresh "- -" "- -"
note a1 "nostr:${happy} say hello" "$happy"
wait_eq a1 lead 1
settle
expect a1 approval 0 "A1 Auto-approve must not ask"
expect a1 job_start 1 "A1 Auto-approve runs the turn"

# A3 auto mode election -> plan -> workers, leader = convener: the plan pass
# has its own wake trigger, so it starts while the election's is held too.
fresh "- -" "- -" Happy
note a3 "nostr:${happy} nostr:${scout} nostr:${builder} plan a picnic together" "$happy" "$scout" "$builder"
wait_eq a3 partner 2
settle
expect a3 approval 0 "A3 Auto-approve must not ask"
expect a3 startfail 0 "A3 every auto pass must start"
expect a3 job_start 4 "A3 election, plan and two workers each run once"
expect_starts elect 1 "A3 the election runs once"
expect_starts plan 1 "A3 the leader's plan pass runs once"
expect_starts lead 0 "A3 Happy runs no ordinary turn"
expect_starts partner 2 "A3 each worker runs once"

# A2: the setting is read and saved by /api/profile (not the dev_log_enabled
# trap), and an approval post leaves the profile names alone.
[ "$(profile '{"first_name":"Chuck","last_name":"P","email":"","organization":"","theme":"dark"}')" = 200 ] \
    || fail "A2 profile save failed"
[ "$(profile '{"approval_mode":"approve_every_action"}')" = 200 ] || fail "A2 approval post refused"
grep -q '"approval_mode":"approve_every_action"' "$home/profile.out" \
    || fail "A2 the profile reply must carry the saved approval_mode"
[ "$(mode)" = approve_every_action ] || fail "A2 session must read back approve_every_action"
session | grep -q '"first_name":"Chuck"' || fail "A2 an approval post must keep first_name"
grep -q '"approval_mode":"approve_every_action"' "$HUSH_CONFIG_DIR/vibe.json" \
    || fail "A2 vibe.json must store approval_mode"
[ "$(profile '{"first_name":"Chuck","last_name":"P","email":"","organization":"","theme":"light"}')" = 200 ] \
    || fail "A2 second profile save failed"
[ "$(mode)" = approve_every_action ] || fail "A2 a profile save must keep approval_mode"
# A4: an unknown value is refused with a named reason; the setting is unchanged.
[ "$(profile '{"approval_mode":"maybe"}')" = 400 ] || fail "A4 an unknown approval_mode must be refused"
grep -q 'approval_mode must be auto_approve or approve_every_action' "$home/profile.out" \
    || fail "A4 the refusal must name the allowed values"
[ "$(mode)" = approve_every_action ] || fail "A4 a refused value must not change the setting"

# G1 first turn: the mention waits; the owner's Yes runs it exactly once.
fresh "- -" "- -"
note g1 "nostr:${happy} tell a joke" "$happy"
wait_eq g1 approval 1
settle
expect g1 ask_happy 1 "G1 the approval line copy"
expect g1 lead 0 "G1 a waiting turn must not run"
expect g1 job_start 0 "G1 a waiting turn must not start a job"
expect_starts lead 0 "G1 a waiting turn must not start the robot program"
reply g1 "Yes"
wait_eq g1 lead 1
settle
expect g1 approval 1 "G1 one approval per turn"
expect g1 job_start 1 "G1 Yes starts one job"
expect_starts lead 1 "G1 Yes starts the robot program once"

# G2 decline: No posts one line and never starts the robot program.
fresh "- -" "- -"
note g2 "nostr:${happy} tell a riddle" "$happy"
wait_eq g2 approval 1
reply g2 "No"
wait_eq g2 declined 1
settle
expect g2 no_happy 1 "G2 the decline line copy"
expect g2 lead 0 "G2 a declined turn must not run"
expect g2 job_start 0 "G2 a declined turn must not start a job"
expect_starts lead 0 "G2 a declined turn must not start the robot program"

# G3 follow wave and loop turn: each turn of a two-robot loop asks first,
# and a waiting loop turn does not run.
fresh "LOOP:_continue LOOP:_stop_done" "- -"
note g3 "nostr:${happy} riddle game, keep going. nostr:${scout} answer." "$happy" "$scout"
wait_eq g3 approval 1
settle
expect g3 lead 0 "G3 the first turn waits"
reply g3 "Yes"
wait_eq g3 approval 2
settle
expect g3 lead 1 "G3 the lead ran after Yes"
expect g3 partner 0 "G3 the follow-wave turn waits"
expect_starts partner 0 "G3 the follow-wave turn must not start the robot program"
reply g3 "yes"
wait_eq g3 approval 3
settle
expect g3 partner 1 "G3 the partner ran after Yes"
expect g3 lead 1 "G3 a waiting loop turn must not run"
expect_starts lead 1 "G3 a waiting loop turn must not start the robot program"
reply g3 "Yes."
wait_eq g3 lead 2
settle
expect g3 approval 3 "G3 the lead's stop ends the loop"
expect g3 job_start 3 "G3 one job per approved turn"
reply g3 "thanks, done here"
wait_eq g3 approval 4
reply g3 "No"
wait_eq g3 declined 1

# G4 cap counting: approval lines are not turns. Cap 2, a live loop: two
# approved turns reach the cap and the loop prompt asks, not a third approval.
set_cap 2
fresh "LOOP:_continue LOOP:_continue -" "- -"
note g4 "nostr:${happy} riddle game, keep going. nostr:${scout} answer." "$happy" "$scout"
wait_eq g4 approval 1
reply g4 "Yes"
wait_eq g4 approval 2
reply g4 "yes"
wait_eq g4 ask 1
settle
expect g4 approval 2 "G4 the cap stops the loop before a third approval"
expect g4 lead 1 "G4 cap counts robot turns only"
expect g4 partner 1 "G4 cap counts robot turns only"
reply g4 "No"
wait_eq g4 stopped 1
set_cap 8

# G5 election, approved plan pass, workers (#279 P1-1). The convener (Happy)
# is elected leader, so its plan pass follows its own election pass on the
# same robot and root. Every pass asks first; each Yes runs exactly one.
fresh "- -" "- -" Happy
note g5 "nostr:${happy} nostr:${scout} nostr:${builder} plan a party together" "$happy" "$scout" "$builder"
wait_eq g5 approval 1
settle
expect g5 elect_ask 1 "G5 the first ask names the leader election"
expect g5 job_start 0 "G5 the election pass must wait"
expect_starts elect 0 "G5 the election pass must not start the robot program"
reply g5 "Yes"
wait_eq g5 approval 2
settle
expect_starts elect 1 "G5 Yes runs the election pass once"
expect g5 ask_happy 1 "G5 the plan pass asks for the elected leader"
expect g5 job_start 1 "G5 the plan pass must wait"
expect_starts plan 0 "G5 the plan pass must not start the robot program"
reply g5 "Yes"
wait_eq g5 plan 1
wait_eq g5 approval 3
settle
expect g5 startfail 0 "G5 an approved plan pass must start"
expect_starts plan 1 "G5 Yes runs the plan pass once"
expect g5 job_start 2 "G5 one job per approved pass"
expect g5 partner 0 "G5 each worker turn waits"
expect_starts partner 0 "G5 a waiting worker must not start the robot program"
reply g5 "Yes"
wait_eq g5 partner 1
wait_eq g5 approval 4
settle
expect g5 partner 1 "G5 the second worker waits for its own Yes"
reply g5 "Yes"
wait_eq g5 partner 2
settle
expect g5 approval 4 "G5 one ask per pass"
expect g5 job_start 4 "G5 election, plan and two workers each run once"
expect_starts partner 2 "G5 each approved worker runs once"
expect_starts elect 1 "G5 the election never reruns"
expect_starts plan 1 "G5 the plan pass never reruns"
expect_starts lead 0 "G5 Happy runs no ordinary turn"
expect g5 startfail 0 "G5 no approved pass fails to start"

# G6 a declined plan pass starts nothing and asks for no worker.
fresh "- -" "- -" Happy
note g6 "nostr:${happy} nostr:${scout} nostr:${builder} plan a dinner together" "$happy" "$scout" "$builder"
wait_eq g6 approval 1
reply g6 "Yes"
wait_eq g6 approval 2
reply g6 "No"
wait_eq g6 declined 1
settle
expect g6 no_happy 1 "G6 the decline line copy"
expect g6 job_start 1 "G6 a declined plan pass must not start a job"
expect g6 approval 2 "G6 a declined plan pass asks for no worker"
expect_starts plan 0 "G6 a declined plan pass must not start the robot program"
expect_starts partner 0 "G6 no worker runs after a declined plan"

# G7 void: another owner note voids the waiting turn; a later Yes answers
# only the turn that note itself raised.
set_cap 8
fresh "- - -" "- -"
note g7 "nostr:${happy} count to three" "$happy"
wait_eq g7 approval 1
reply g7 "actually, never mind"
wait_eq g7 approval 2
reply g7 "Yes"
wait_eq g7 lead 1
settle
reply g7 "Yes."
wait_eq g7 approval 3
settle
expect g7 lead 1 "G7 a voided turn must never run"
expect_starts lead 1 "G7 a voided turn must never start the robot program"

# R1 restart: the setting survives; a turn waiting at restart never runs.
fresh "- -" "- -"
note r1 "nostr:${happy} wait for me" "$happy"
wait_eq r1 approval 1
stop_relay
start_relay
[ "$(mode)" = approve_every_action ] || fail "R1 the setting must survive a restart (got '$(mode)')"
reply r1 "Yes"
wait_eq r1 approval 2
settle
expect r1 lead 0 "R1 a turn waiting at restart must never run"
expect_starts lead 0 "R1 a turn waiting at restart must not start the robot program"

# A5 garbled or missing stored value reads as the default, auto_approve.
stop_relay
sed -i 's/"approval_mode":"approve_every_action"/"approval_mode":"maybe"/' "$HUSH_CONFIG_DIR/vibe.json"
grep -q '"approval_mode":"maybe"' "$HUSH_CONFIG_DIR/vibe.json" || fail "A5 garble step"
start_relay
[ "$(mode)" = auto_approve ] || fail "A5 a garbled stored value must read as auto_approve (got '$(mode)')"
echo "approve checks ok"
