# GUARD: per-command guardrails (plan only)

Status: PLAN. No product code. Owner: Floor (engine, relay, API). Journeys (JNY): UI. Order: CoS 2026-10-17 09:56 ET, user-specified by Chuck.
Base: main `13511bd48b47` (#323). Depends on C-5 P1 = #336 "P1 security: agent children can read session.token and self-approve; no Origin/CSRF check" (CSRF/Origin, approve nonce, agent sandbox, loopback `Set-Cookie` owner-token hand-out), which ships first. Related: #333 (Copilot/Goose ungated), #337 (persist approval-held turns across relay restart).
CoS ruling 2026-10-17 10:12 ET applied (rev 2); see §10.
Tags: **OBS** `file:line` at `13511bd`, **VERIFIED_EXTERNAL** url, **INFERRED**, **UNKNOWN**.

## 0. Spec (from the order)

- Per shell command, the owner decides: **approve once | always approve | ask every time | reject**.
- Always and reject persist as rules. The owner can list, edit and delete rules, and add custom rules directly.
- Rule fields: `match` (exact/prefix/pattern), `scope` (robot/project/global), `action`, `created_by`, `created_at`, `hit_count`.
- Default is ask. Reject beats approve. Robots cannot write rules; they only propose them (mirrors KEYS).
- Enforced on the relay before exec, for every shell-capable provider. Every decision is audited.

## 1. Current exec path (main `13511bd`)

| Item | Finding | Tag |
|---|---|---|
| Tip | `13511bd48b4784746daf0645e7351591ca01d76a` "Gate Cline auto-approve behind a person" (#323) | OBS (GitHub API) |
| Fork | `hush_agent_spawn_grok` forks a worker (`agent_process.c:597`); `capture_worker` also forks (`:190`) | OBS |
| Child | `hush_agent_exec_child` (`agent_process.c:279-300`): dup2 stdout to the pipe, stderr to /dev/null, dispatch by `job->provider`. **No env scrub, no sandbox, stdin inherited.** | OBS |
| Cline | `cline --json --cwd <cwd> --auto-approve <true\|false> --timeout N <prompt>` (`:380-401`). `true` only when `project_tools` is set (`:373-378`) | OBS |
| Grok Build | `grok -p … --always-approve … --disallowed-tools <list>` (`:468-499`). Both deny lists contain `run_terminal_cmd` (`:26-29`), **so Grok has no shell today** | OBS |
| Copilot | `copilot -p <prompt> --allow-all-tools [-C cwd]` (`:40`, `:440-465`). **Full shell, not gated by `project_tools`** | OBS |
| Codex | `codex exec --cd <cwd> --skip-git-repo-check --sandbox read-only <prompt>` (`:529-545`). Shell runs, but in Codex's read-only sandbox | OBS |
| Goose | `goose run --text <prompt>` after chdir (`:547-562`). No `GOOSE_MODE` set, so Goose's default `auto` applies (no prompts) | OBS + VERIFIED_EXTERNAL https://github.com/block/goose/blob/main/documentation/docs/guides/config-files.md |
| Ollama / API | `ollama run <model> <prompt>` (`:565-584`), API path (`:330`). Neither has a tool loop, so no shell | OBS |
| Turn gate | `hush_agent_approval_needed` (`agent_approve.c:40-46`) holds a whole **turn** under `approve_every_action` (`hush_roster.h:36-37,96-97`). Hold at `agent_dispatch.c:1363-1364`. The answer is an owner "Yes" note (`agent_approve.c:90`). `begin_approved` sets `approved` (`agent_dispatch.c:1386-1391`) | OBS |
| Person gate (#322/#323) | `person_gate = in->approved \|\| owner_asked(in)` (`agent_prompt.c:153`) → `apply_project_cwd` sets `project_tools` (`agent_team.c:321-345`) | OBS |
| HTTP auth | Session token by cookie, bearer or query (`hush_http.c:530-551`); 401/403 helper (`:187`); `/api/session` (`:307`). `/api/turn` (`:323`, `:1051`), `/api/profile` (`:1013`). **No Origin or CSRF check and no nonce** (searched `Origin\|Sec-Fetch`: no hits in `hush-c/src`) | OBS |
| macros.json | **Not on main.** Planned in `docs/plan/PLAN_KEYS.md` §5.2 on branch `gb/keys-plan` (`59b48543`): `hush/macros.json`, mode 0600, written only by the relay, `GET/POST /api/macros` | OBS (branch) |
| KEYS propose | **Not on main.** Planned in PLAN_KEYS §0: a system skill `skills/system/keys-macros/SKILL.md` emits a fenced `hush-keys` proposal; the owner applies it by hand; robots get no session token | OBS (branch) |
| GUARD note | PLAN_KEYS §2 already reserves a GUARDRAILS tab and the Approve / Always approve / Ask every time / Reject stamps; it notes that the `approve_guest_turns` mode must never write Always rules and that explicit Reject/Ask beats the mode | OBS (branch) |

Gap: today's approvals are per **turn**. Inside a turn, Copilot (always) and Goose (always), and Cline when `project_tools` is set, run any shell command with no Hush check. Codex runs read-only commands.

## 2. Feasibility: can Hush decide before exec?

Hush runs external CLIs, and those CLIs run the shell themselves. GUARD is only real when the CLI asks Hush before every command. The plan uses three mechanisms:

- **(H) Hook.** The CLI runs a Hush-owned hook binary (`hush-guard-hook`) before each tool call. The hook asks the relay over a unix socket and blocks until the decision comes back.
- **(P) Protocol.** Hush drives the CLI over JSON-RPC (ACP or app-server) and answers its permission requests.
- **(M) MCP.** The native shell tool is disabled and Hush serves a `shell` tool over MCP that enforces the rules.

Wrapper shells via PATH or SHELL are **rejected as an enforcement mechanism**: CLIs exec `/bin/bash` by absolute path, and a model can call `env -i`, `python -c` and so on (INFERRED). They may be used for logging only.

| Provider | Today (OBS) | Officially supported interception | Pre-exec? | GUARD mechanism | Source |
|---|---|---|---|---|---|
| Cline | shell only when `--auto-approve true` | `PreToolUse` hook; `run_commands` input `command`; returns `cancel` / `review`. CLI path `.cline/hooks/PreToolUse.sh`; **hooks are disabled in `--yolo`** | **Yes** | H. Install the hook in a Hush-owned HOME, never in the project cwd (agent-writable). UNKNOWN: whether `--auto-approve true` keeps hooks on (a pin test is needed); whether a global CLI hook dir exists | VERIFIED_EXTERNAL https://github.com/cline/cline/blob/main/sdk/examples/hooks/README.md , https://docs.cline.bot/usage/cli-overview , https://github.com/cline/cline/pull/13649 |
| Copilot CLI | `--allow-all-tools` (#333) | `preToolUse` hook, `permissionDecision` allow/deny/ask; exit 2 or a crash fails closed. **A timeout always fails open, "including for `preToolUse` and admin-deployed policy hooks"; HTTP hooks fail open too. No documented setting makes a timeout deny.** `permissionRequest` hooks also fall through on no decision. `--deny-tool 'shell'` beats every allow | **No** (not fail-closed) | **Native shell denied** (`--deny-tool shell`, no `--allow-all-tools`). Shell only via the Hush MCP `shell` tool (M), allowed as `--allow-tool '<hush-mcp>(shell)'`. The hook may stay as defence in depth, never as the gate. A pinned minimum version and a startup deny self-test are required, or Copilot shell is refused (G5; CoS 10:13). #333 is a prerequisite. UNKNOWN (pin later, not relied on): whether a timed-out hook's fall-through in `-p` mode with no allow rule denies | VERIFIED_EXTERNAL https://docs.github.com/en/copilot/reference/hooks-reference , https://docs.github.com/en/copilot/how-tos/copilot-cli/use-copilot-cli/allowing-tools , https://docs.github.com/en/copilot/reference/copilot-cli-reference/cli-command-reference (`--excluded-tools`, `--additional-mcp-config`, `--trust-folder`, `--disable-builtin-mcps`, `COPILOT_ALLOW_ALL`, `GITHUB_COPILOT_PROMPT_MODE_*`) |
| Codex | `exec --sandbox read-only` | app-server JSON-RPC `item/commandExecution/requestApproval` → accept/decline/cancel; `PreToolUse` hooks (non-managed hooks need trust review); execpolicy `prefix_rule` | **Yes** (P); H is secondary | P: move from `codex exec` to `codex app-server` with approval policy `untrusted`, so every command reaches Hush. Hush must never send `acceptWithExecpolicyAmendment` unless the owner chose Always | VERIFIED_EXTERNAL https://learn.chatgpt.com/docs/hooks , https://github.com/openai/codex/blob/main/codex-rs/execpolicy/README.md , https://www.mintlify.com/openai/codex/concepts/approvals |
| Goose | `goose run`, default mode `auto` | `GOOSE_MODE=approve`; ACP `requestPermission` with allow_once / allow_always / reject_once / reject_always; per-tool `permission.yaml` | **Yes** via ACP (P) | P: `goose acp`, answer `requestPermission` per call; map allow_always/reject_always to Hush-only rules and reply `*_once` to Goose so Goose keeps no rule store | VERIFIED_EXTERNAL https://block-goose.mintlify.app/advanced/acp-protocol , https://github.com/block/goose/blob/58f3cc9e/documentation/docs/guides/goose-permissions.md |
| Grok Build | shell denied (`run_terminal_cmd` in both deny lists) | `PreToolUse` hooks in `~/.grok/hooks/*.json` (matcher `Bash`), deny via JSON or exit 2; `--always-approve` keeps deny rules and hooks; **hook failure fails open** | Yes, but fail-open | Keep shell denied (today). Re-enable only with H **plus** M fallback; fail-open makes H alone non-enforcing | VERIFIED_EXTERNAL https://docs.x.ai/build/features/hooks , https://docs.x.ai/build/features/permissions |
| Hermes Agent (planned) | not wired | plugin `pre_tool_call` returning `{"action":"block"}`; ACP bridges approvals (allow_once/allow_session/allow_always/deny; timeout = deny); smart mode uses an aux LLM; container backends skip approvals | **Yes** (ACP or plugin) | P (ACP), with approvals mode `manual`; the plugin as a second layer. Never a container backend without GUARD | VERIFIED_EXTERNAL https://hermes-agent.nousresearch.com/docs/user-guide/security , https://hermes-agent.nousresearch.com/docs/user-guide/features/hooks , https://github.com/NousResearch/hermes-agent/pull/9377 |
| OpenClaw `--isolated` (planned) | not wired | exec approvals: `tools.exec.mode ask` (allowlist + on-miss) / `always`; `askFallback` defaults to deny with no UI; chains need every segment allowlisted | Yes (gateway/companion approval flow) | UNKNOWN how an external client answers prompts in `--isolated`/`--local` (no UI = askFallback). Plan: `ask: always` + `askFallback: deny` + `security: deny` for host exec, and M as the only shell | VERIFIED_EXTERNAL https://docs.openclaw.ai/tools/exec-approvals , https://docs.openclaw.ai/tools/exec , https://github.com/openclaw/openclaw/issues/59720 |
| NanoClaw (planned) | not wired | runs the Claude Agent SDK in a container; the SDK has `PreToolUse` hooks (permissionDecision) | Yes in principle | UNKNOWN whether NanoClaw exposes a hook/config seam to an outside parent. Default: deny Bash, offer M | VERIFIED_EXTERNAL https://github.com/nanocoai/nanoclaw/blob/87a2d3af/container/agent-runner/src/providers/claude.ts , https://github.com/anthropics/claude-agent-sdk-python/blob/main/examples/hooks.py |
| NemoClaw (planned) | not wired | OpenShell sandbox: deny-by-default **network** policy with operator approval; no documented per-command exec approval | **No** documented | Inherits OpenClaw exec approvals inside (INFERRED). Default: shell denied, M only | VERIFIED_EXTERNAL https://docs.nvidia.com/nemoclaw/user-guide/openclaw/reference/network-policies |
| Ollama / API | no tools | n/a | n/a | none needed | OBS |

**True per-command enforcement (target, each only after its stub proof passes; CoS 10:13):** Codex (P), Hermes (P). **Conditional on a stub proof, shell denied until then:** Cline (H; UNKNOWN whether hooks stay on under `--auto-approve true`) and Goose (P; UNKNOWN whether ACP `requestPermission` fires for every shell call, so `GOOSE_MODE=approve` is set in the child env). **Native shell denied; shell only through the Hush MCP `shell` tool (M):** Copilot (hook timeout fails open; CoS 10:12: unacceptable), Grok Build, OpenClaw, NanoClaw, NemoClaw. Grok's `--always-approve` (`agent_process.c:480`) for non-shell tools is out of GUARD scope (tracked with #332).

Fail-closed contract for every **gating mechanism** (the decision path only; see the limits in §3, since GUARD does not cover code that an approved command runs): socket down, malformed reply, relay timeout, or hook crash → **deny**. A seam with a documented fail-open edge (Copilot timeout, Grok hook failure) is not a gate; those providers use M. The M server itself fails closed.

**Copilot fail-closed finding (CoS 10:12).** VERIFIED_EXTERNAL https://docs.github.com/en/copilot/reference/hooks-reference ("Exit codes for command hooks" and the `preToolUse` IMPORTANT note): "Timeouts are fail-open for every event, including `preToolUse` and admin-deployed policy hooks"; "HTTP `preToolUse` hooks are fail-open". `timeoutSec` (default 30) can be raised, but no option turns a timeout into a deny. So Copilot gets no native shell.

## 3. Matcher semantics

- **Input.** For P, use the argv the CLI reports (Codex sends an argv array). For H and M, use the command string, parsed by one relay-side POSIX-sh tokenizer (`hush_guard_parse`). Never match on the raw string.
- **Kinds.**
  - `exact`: token-for-token equality after normalisation.
  - `prefix`: token prefix, as in Codex `prefix_rule`; `git push` matches `git push origin` but not `git pushx`.
  - `pattern`: **glob, anchored on the whole normalised command**; `*` does not match `;|&<>$()\`` or a newline. No regex: it is hard to read, easy to leave unanchored, and has ReDoS risk.
- **Normalisation.** Strip leading `VAR=val` env prefixes, but record them; any env prefix on `LD_*`, `PATH`, `BASH_ENV` or `PROMPT_COMMAND` falls to ask. Resolve `argv[0]` to its basename only when the path is in a fixed system set. Undo quoting. Collapse whitespace.
- **Compound commands.** Split on `;`, `&&`, `||`, `|`, `&` and newlines. **Every segment must match an approve rule.** Any segment matching reject → reject. Otherwise → ask. This mirrors OpenClaw's allowlist (VERIFIED_EXTERNAL docs.openclaw.ai/tools/exec).
- **Always ask** (rules cannot approve): subshells, `$(…)`, backticks, `<(…)`, `eval`, `exec`, `source`/`.`, `sh|bash|zsh -c`, `python|node|perl|ruby -e/-c`, `xargs`, `find -exec`, heredocs, redirections to paths outside the cwd, `sudo`/`doas`, and any parse error.
- **Bypass risks** (each gets a test): quote splitting (`g""it push`), `\` newline, absolute paths (`/usr/bin/git`), aliases or functions defined earlier in the same command, `command`/`builtin` prefixes, unicode look-alikes (reject non-ASCII in `argv[0]`), long-command truncation (over 8 KiB → ask), and a TOCTOU gap between the hook seeing a command and the CLI running it (P and M avoid it; H trusts the CLI to run what it showed).
- **Limits, stated plainly (Gauge N2, CoS 10:31).** GUARD decides on the **command text**. It cannot see what that command will run. File edits are not gated (§6). So an agent can write code that a harmless-looking approved command later runs: `.git/config` (`core.fsmonitor`, `core.pager`, aliases), `.git/hooks/*`, a Makefile, `package.json` scripts. Two mitigations:
  - **Git children run with pinned config:** `GIT_CONFIG_NOSYSTEM=1`, `GIT_CONFIG_GLOBAL=/dev/null`, plus `GIT_CONFIG_COUNT` pins `core.hooksPath=/dev/null`, `core.fsmonitor=false`, `core.pager=cat`. Alias expansion is refused (INFERRED: repo-local `.git/config` still loads, and the pins override only the keys they name; Floor to confirm the full key list).
  - **Script runners are always-ask and never eligible for Always:** `make`, `npm|pnpm|yarn|bun run|test|exec`, `npx`, `cargo build|run|test`, `pytest`, `tox`, `go test|run|generate`, `./<anything>`, and `git` subcommands that can run hooks or filters (`commit`, `merge`, `rebase`, `checkout`, `am`, `push`, `pull`). The card warns with `[[GUARD_RUNS_PROJECT_CODE]]` (copy TBD, JNY/Ops).
  - Residual risk is accepted and stated on the card: an approve-once still runs whatever the project files say.

## 4. Precedence

1. A hard deny (fixed set: anything touching `$HUSH_HOME` or the hook config, and **any command that reaches the relay port or loopback API**, e.g. `curl 127.0.0.1:<port>`) → reject; no rule can override it. The matcher check is best-effort. The real block is the #336 sandbox network rule that denies agent connections to the relay host:port (CoS 10:13; INFERRED: H alone cannot enforce it).
2. Collect every rule that matches, at **any** scope. **Any reject → reject.** Reject beats approve across scopes, so a global reject cannot be overridden by a robot approve.
3. Ask rules: the most specific scope's ask beats approve at the same or a broader scope. Specificity: robot > project > global.
4. Approve applies only when every segment has an approve and no reject or ask applies.
5. No match → **ask** (default).

Turn gate interaction: GUARD runs inside an already-approved turn. `approve_every_action` (turn) and GUARD (command) are independent. The KEYS `approve_guest_turns` mode never creates or satisfies GUARD rules (PLAN_KEYS §2).

**GUARD is an extra layer, never a replacement (CoS 10:13).** `person_gate` (`agent_prompt.c:153`) and `project_tools` (`agent_team.c:321-345`) stay required for any shell. #333 (Copilot/Goose without the person gate) is a prerequisite. **Always rules never apply to guest asks:** in a turn that is not owner-asked, approve rules are skipped and every command asks the owner; reject rules still apply. Known residual: `owner_asked` treats a robot note in an owner-opened thread as the owner's ask (`agent_approve.c:136-155`, #331 P3-4), so **GUARD does not reuse `owner_asked` for Always eligibility** (N5). GUARD signal: **if the author of the job's direct parent note is a robot (or not the owner), the turn is chained/guest → no Always**, every command asks, and reject still applies. Only a direct-parent note authored by the owner's pubkey is Always-eligible.

**Resolved (JNY ruling 10:10): Always-approve rules exclude robot-chained, follow and election turns by default.** Commands in those turns skip approve rules and go to ask; reject rules still apply. A per-rule opt-in (`turns: ["chained","follow","election"]`) comes later and needs Gauge review. It is not in v1.

## 5. Rule authority and audit

- **Writes.** Only the owner, through the UI, with the session cookie, plus the **C-5 approve nonce bound to the browser session** (C-5 fix b), plus a matching `Origin`/CSRF check (C-5 fix c). Bearer-only and query-token calls cannot write rules. **INFERRED**: a remote client holding the token is not the owner's browser.
- **Robots.** Propose only. This mirrors the KEYS flow: a system skill `skills/system/guard-rules/SKILL.md` emits a fenced `hush-guard` v1 block. The UI shows it as a proposal card, and the owner presses Apply, which goes through the nonce path. The relay never parses robot output into rules on its own.
- **Decisions.** Decisions are made **only on the pending card** (JNY 10:13). "Always approve" and "Reject" create a rule (`created_by: owner`, `source: decision`). "Ask every time" stores an `ask` rule. **Always defaults to `exact` match at `robot` scope.** The scope pick may widen it to project or global. Prefix and glob are set only by editing in the rules table or adding a custom row, never from the card.
- **Audit.** `hush/guard-audit.jsonl`, append-only, mode 0600, relay-written, one JSON line per decision or rule change. Fields: `ts, event (decide|rule_create|rule_update|rule_delete|propose), robot, provider, project, thread_root, job_id, cmd_redacted, cmd_sha256, segments, decision, rule_id, by (owner|rule|default|timeout|failclosed), mechanism (H|P|M)`.
- **Redaction** before writing: values of `--token/--password/--secret/--key`, `Authorization`/`Bearer`, `ghp_/gho_/ghs_/xai-/sk-` shapes, URL userinfo, and env prefix values. The full command is kept only as `cmd_sha256`.
- **Retention.** Rotate at 5 MiB, keep 5 files (**approved, CoS 10:12**). Never synced over nostr.
- **Pending asks (CoS 10:12, 10:13).** Deadline = created + `ask_timeout_s` (default **600 s**, owner-configurable through the nonce path; Floor sets the bounds), measured on **CLOCK_MONOTONIC**. On expiry the ask is **auto-rejected**: the agent receives `rejected: timeout`, the thread shows the visible reason (`GUARD_EXPIRED`), and the audit logs `by: timeout`. Decided-vs-expired is one atomic compare-and-set. **A late Yes after expiry is refused** (HTTP **410 Gone**, the same code for every expired or already-ended ask, plus a message on the card), never applied, and audited. **Restart (N4):** a relay restart **always ends** every pending ask as `rejected: relay restart`. The hook connection drops, so the CLI already got a fail-closed deny (`by: failclosed`). The record persisted under **#337** (whose scope must be extended to include GUARD pending asks, N6) is **display-only**: the card shows expired, any decision on it returns 410, and nothing is re-armed. Downtime does not matter because no deadline survives a restart. **Clocks:** the provider hook timeout is set to `ask_timeout_s + 60 s` (Cline). Copilot uses no gating hook. The Hush job budget is also kept above `ask_timeout_s`. If the job clock still fires first, the job is killed and the ask is rejected `by: job_end`. The job clock wins.

## 6. Shared exec path with C-5 P1

C-5 is tracked as **#336** (verified title: "P1 security: agent children can read session.token and self-approve; no Origin/CSRF check"). Its branches (`gb/p1-csrf-origin`, `gb/p1-approve-nonce`, `gb/p1-agent-sandbox`) did not exist at 09:56 ET. #336 also covers the P1 **loopback `Set-Cookie` owner-token hand-out** (`hush_http.c:286-290`, `:732-738` per #336). GUARD depends on that being closed: otherwise a sandboxed child fetches the token over loopback and writes rules. **#336 is a prerequisite for G3 and G5-G9 (CoS 10:13)**. It must cover (1) the loopback `Set-Cookie` bootstrap, (2) forged owner-stamped notes (`api_channels.c:102` → `owner_asked` `agent_approve.c:140-154` → `person_gate` `agent_prompt.c:153`; #336 Q2/Q3), and (3) a hard deny on agent shell reaching the relay port or loopback API. GUARD builds on C-5:

- **Sandbox** (landlock/bwrap) in `exec_child` (`agent_process.c:279`) must deny read and write on `$HUSH_HOME`, which covers `guardrails.json`, `guard-audit.jsonl`, `session.token` and the guard socket directory except the one socket bound in. The hook binary and its config sit read-only, outside the cwd.
- **Env scrub** in the same place: remove `HUSH_*`, tokens and keystore variables; set `HOME` to a per-job Hush-owned dir. The provider hook config (e.g. `~/.grok/hooks`, `.cline/hooks`, Copilot hooks) is **bind-mounted read-only** into that HOME; the rest of HOME stays writable for the CLI. Non-shell write tools (file edits) are not gated by GUARD, so the read-only mount is the guard. Test: the stub agent tries to overwrite or delete the hook config and fails, and the hook still runs (write mutant: mount read-write → test fails).
- **Socket.** One unix socket per job, carrying `job_id` and a per-job random secret passed by fd (not env). The relay checks `SO_PEERCRED` against the job's pid tree.
- **Order.** C-5 merges → GUARD M1.

## 7. Data, API, UI contract

**Store: a separate `hush/guardrails.json` (RESOLVED, CoS approved 10:12)** (mode 0600, relay-only, atomic write by tmp + rename). Not shared with `macros.json`, because:

1. It is a security boundary with a different blast radius. A bad macro must not corrupt rules; PLAN_KEYS chose a separate file for the same reason (C-7).
2. It changes on a different path: GUARD writes need the nonce + Origin and count `hit_count` on every hit, while macros are edited rarely.
3. The sandbox deny list and audit can name it precisely.
4. `macros.json` does not exist yet.

The UI can still share the drawer (a GUARDRAILS tab). JNY to confirm.

```json
{
  "format": "hush-guard", "version": 1,
  "settings": {"ask_timeout_s": 600},
  "rules": [
    {"id": "g_01J…", "match": {"kind": "prefix", "value": ["git", "status"]},
     "scope": {"kind": "project", "id": "hush"}, "action": "approve",
     "created_by": "owner", "source": "decision", "created_at": "2026-10-17T14:00:00Z",
     "updated_at": "2026-10-17T14:00:00Z", "hit_count": 0, "last_hit_at": null, "note": ""},
    {"id": "g_01K…", "match": {"kind": "pattern", "value": "rm -rf *"},
     "scope": {"kind": "global"}, "action": "reject", "created_by": "owner",
     "source": "custom", "created_at": "…", "updated_at": "…", "hit_count": 3, "last_hit_at": "…", "note": ""}
  ]
}
```

Stored `action` enum is exactly `approve|reject|ask` (JNY 10:13). `ask` is stored because "Ask every time" needs it. "Approve once" is never stored. `scope.kind` is one of `robot|project|global`, and `id` is the robot hex or the project slug.

**API** (all need the session cookie; mutations also need `X-Hush-Nonce` + same Origin):

| Method | Route | Purpose |
|---|---|---|
| GET | `/api/guard/rules` | list rules |
| POST | `/api/guard/rules` | create a rule (custom or apply-proposal) |
| PATCH | `/api/guard/rules/{id}` | update match/scope/action/note |
| DELETE | `/api/guard/rules/{id}` | delete a rule |
| GET | `/api/guard/pending` | pending commands (robot, provider, project, cmd_redacted, segments, age, expires) |
| POST | `/api/guard/pending/{id}` | `{decision: once\|always\|ask\|reject, match?, scope?}`; any expired, restart-ended or already-decided ask returns **410** with the reason; nothing is applied |
| GET | `/api/guard/audit?limit=` | tail of the audit log (redacted) |

Route style follows `/api/*` in `hush_http.c:1005-1060`. Path parameters are **NEW** to the router (INFERRED: today it uses `strcmp` exact paths), so they may need to become query parameters, e.g. `?id=`. Floor to decide.

**UI hand-off (JNY).** No colours are set here; JNY uses existing tokens.

- **Placement (RESOLVED, JNY 10:13).** The pending card sits inline in the thread the command came from and carries the 4 S1 stamps. The rules table is the GUARDRAILS tab in the Shortcuts & Macros drawer (PLAN_KEYS §3). The tab may show a pending-count badge that links to the thread card. JNY's UI work starts after #325 + W1/W2.
- **States.** Pending card: waiting (with a countdown to auto-reject) / deciding / decided (once, always, ask, reject) / expired (auto-reject, visible reason) / failed-closed / rejected-by-restart. Rules table: empty / list / editing / delete-confirm / save-error / nonce-expired. Proposal card: proposed / applied / dismissed.
- **Stamps.** Approve | Always approve | Ask every time | Reject (PLAN_KEYS §2).
- **Copy (PROPOSED by JNY 10:13; Ops owns the final tone; freeze via Ops/CoS):**
  - `GUARD_PENDING_TITLE`: "{robot} wants to run a command"
  - `GUARD_SEGMENT_HINT`: "This command has {n} parts. Each part is checked."
  - `GUARD_ALWAYS_SCOPE_PICK`: "Always approve this exact command for: {robot} | this project | everywhere"
  - `GUARD_EXPIRED`: "No answer in time, so the command was rejected."
  - `GUARD_FAILCLOSED`: "Hush couldn't check this command, so it was blocked."
  - `GUARD_PROPOSAL_FROM_ROBOT`: "{robot} suggests a guardrail. Nothing changes until you apply it."
  - `GUARD_NO_SEAM_PROVIDER`: "This robot's shell is off: Hush can't see its commands to check them."
- **Must show:** the redacted command, its split segments, the matched rule (if any), robot, project and provider.

## 8. Milestones (each a small DRAFT; after C-5)

Every PR includes stub tests with a stub PATH (fake `cline`, `copilot`, `codex`, `goose`, `grok` scripts) and a temp HOME and HUSH_HOME. Real tools are never spawned. Canary = 0 (the stubs fail the suite if a real binary is found first on PATH). Each PR lists reverse mutants and goes green on `ci.yml`.

| # | Draft | Scope | Key tests / reverse mutants |
|---|---|---|---|
| G0 | this plan | docs | — |
| G1 | `hush_guard` core | parser, matcher, precedence, `guardrails.json` load/save; no wiring | table tests for §3 bypasses. Mutants: drop the segment split → `a && rm` approved; reject-last ordering → approve wins; regex instead of anchored glob; script runner (`make test`, `npm test`) accepted as an Always rule; git child without pinned config (planted `core.fsmonitor` runs on Always `git status`) |
| G2 | audit + redaction | jsonl writer, rotation | Mutant: skip redaction → `ghp_` appears in the log |
| G3 | API + nonce | `/api/guard/*` on the C-5 nonce/Origin | Mutants: accept bearer-only writes; skip the Origin check |
| G4 | guard socket + `hush-guard-hook` + pending expiry | socket, peercred, fail-closed, 10 min auto-reject (configurable), visible reason | Mutants: on timeout return allow; expiry posts no reason |
| G5 | Copilot (M) | prereq #333 + #336. argv: `--deny-tool shell` **and** `--excluded-tools bash,powershell,write_bash,write_powershell,read_bash,read_powershell,list_bash,list_powershell,stop_bash,stop_powershell`; no `--allow-all-tools`; **never `--trust-folder`**; `--disable-builtin-mcps`; Hush MCP shell registered **only** via `--additional-mcp-config` and allowed as `--allow-tool '<hush>(shell)'`. Job `$COPILOT_HOME/config.json` has **empty `trustedFolders`**. Child env scrubs every `COPILOT_*` and `GITHUB_COPILOT_*` variable (incl. `COPILOT_ALLOW_ALL`, which auto-loads repo hooks, and `GITHUB_COPILOT_PROMPT_MODE_{EXTENSIONS,REPO_HOOKS,WORKSPACE_MCP}`), then sets only `COPILOT_HOME`. **Deterministic readiness check (N7):** Copilot version >= the pinned minimum, and `copilot help` lists `--deny-tool`, `--excluded-tools`, `--additional-mcp-config`, `--disable-builtin-mcps`. Hush builds the argv and a stub captures it to confirm. No model call, no credits. Fail or unknown → not-ready, turn refused (HUSH_JOB_ERR), no fallback. The user hook is not part of readiness | stub copilot argv/env pins (real Copilot never spawned). Mutants: re-add `--allow-all-tools`; drop `--deny-tool shell`; drop `--excluded-tools`; add `--trust-folder` → planted `.github/hooks/*.json`, `.github/copilot/settings.json` hooks, `.claude/settings.json`, `.github/agents` hooks, `.mcp.json`/`.github/mcp.json` in the project cwd take effect (the test asserts they do not); `COPILOT_ALLOW_ALL` / `GITHUB_COPILOT_PROMPT_MODE_*` left set; `trustedFolders` non-empty; version unknown → runs |
| G6 | Cline (H) | hook read-only in the job HOME; stub-prove hooks stay on with `--auto-approve true`; **if the proof fails, Cline shell stays denied** (`--auto-approve false`) | Mutants: hook written into the project cwd; hook mount read-write; proof failure still grants shell |
| G7 | Codex (P) | app-server client | Mutant: auto `acceptForSession` |
| G8 | Goose (P) | `goose acp` with `GOOSE_MODE=approve` in the child env; stub-prove that `requestPermission` fires for every shell call; **otherwise Goose shell stays denied** (developer shell tool disabled) | Mutants: forward `allow_always` to Goose; GOOSE_MODE unset; proof failure still grants shell |
| G9 | MCP `shell` (M) | Hush MCP server; native shell off per provider | Mutant: native shell left enabled |
| G10 | UI (JNY) | pending card, rules tab, proposal card | `check_guard_ui.cjs` with headless stub |
| G11 | `guard-rules` skill | propose-only | Mutant: relay auto-applies a proposal |
| G12 | planned providers | Hermes (P), OpenClaw/NanoClaw/NemoClaw (M) | per-provider no-seam → no shell |

## 9. Open questions

- **CoS:** Sandbox fallback and loopback cookie decisions are tracked in #336. (#333 and #336 are prerequisites, resolved 10:13.)
- **JNY:** countdown display on the card. (Placement, Always default and copy were resolved 10:13.)
- **Floor:** path parameters vs query parameters in the router; bounds for `ask_timeout_s`; Cline `--auto-approve true` + hooks behaviour; OpenClaw isolated-mode prompt seam; #337 scope extension to GUARD pending asks (or a new GUARD issue), needed before G4.

## 10. Resolved (CoS 10:12, JNY 10:10)

| Item | Ruling |
|---|---|
| Pending ask | 10 min, then auto-reject; owner-configurable; the reject shows a visible reason (§5) |
| Audit retention | 5 MiB x 5 files (§5) |
| Copilot | hook timeout fails open = unacceptable. No official fail-closed timeout setting exists (VERIFIED_EXTERNAL above), so native shell is denied and Copilot gets the Hush MCP shell only (§2, G5) |
| Store | separate `hush/guardrails.json` (§7) |
| Always vs chained/follow/election | excluded by default; per-rule opt-in later, Gauge-reviewed (§4) |
| UI placement (JNY 10:13) | inline pending card in the source thread with the 4 S1 stamps, decisions only there; rules table = GUARDRAILS tab (PLAN_KEYS §3) with an optional pending badge; JNY starts after #325 + W1/W2 |
| Always default (JNY 10:13) | exact match at robot scope; scope may widen; prefix/glob only via table edit or custom row |
| Action enum (JNY 10:13) | stored `approve\|reject\|ask` |
| Copy (JNY 10:13) | PROPOSED strings in §7, pending Ops |
| Gauge FAIL @7236f3cd (10:13 CoS) | Copilot version pin + self-test, refuse on fail (G5); monotonic 10 min deadline, persisted via #337, late Yes refused, agent sees `rejected: timeout` (§5); #336 prerequisite incl. Set-Cookie, forged owner notes, relay-port deny (§4, §6); Always never for guest asks, person_gate/project_tools stay required (§4); unknown seams denied until stub-proven (§2, G6, G8); hook config mounted read-only with a write test (§6) |
| Gauge r2 FAIL @7126ce09 (CoS 10:31) | N1 Copilot non-native routes closed (G5); N2 limits stated, git config pinned, script runners always-ask, mutants (§3, G1); N3 410 everywhere; N4 restart ends asks, record display-only; N5 direct-parent author rule; N6 #337 must include GUARD pending asks; N7 deterministic readiness check |
| Tracking | #333 Copilot/Goose ungated; #336 C-5 + loopback `Set-Cookie` owner token (GUARD depends on it); #337 persisted held turns (pending asks) |
