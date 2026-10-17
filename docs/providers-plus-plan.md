# PROVIDERS+ phase 1: research and plan (Hermes, OpenClaw, NVIDIA NemoClaw, NanoClaw)

Status: plan only. This PR adds no product code. Base: `main` @ `13511bd48b47` (#323).
Refs #322, #311 (person gate), #333 (Copilot/Goose gate fix), #336 (C-5: token, Set-Cookie, owner notes), #335 (GUARD, target shell path). Researched 2026-10-17 (America/Toronto). CoS rulings added 09:45, 09:48, 10:12 and 10:38 ET. Gauge reviews of 158c050b and 4acd4c3c addressed in section 7.

Tags: **V** = VERIFIED_EXTERNAL (URL given), **I** = INFERRED, **U** = UNKNOWN.

## 0. CoS rulings (2026-10-17)

| Q | Ruling | Effect on this plan |
|---|---|---|
| Q1 Hermes | **Hermes Agent CLI** | M1 targets `hermes`. The Hermes 4 model family is out of scope; it stays reachable through `custom`. |
| Q2 NVIDIA | **NemoClaw**, `NVIDIA/NemoClaw` (Chuck, 09:48) | M3 targets NemoClaw (section 3). |
| Q3 NanoClaw | **GO**, only behind the #322 person gate | Turns without the gate are refused with a clear reason; Docker isolation documented (section 3). |
| Q4 OpenClaw | **`--isolated`** | `agent exec` ignores the operator's ambient config [V7]. It does **not** isolate credentials: stored credentials are still used (section 3). |
| Q5 Copilot/Goose | **YES**: P1 issue **#333**, owned by Floor | Fix lands before M1. |

**Order (CoS 10:12):** #336 fix, then #333 fix, M1 Hermes, M2 OpenClaw, M4 NanoClaw, M3 NemoClaw.

**10:12 rulings, applied in sections 3-5:**

- Hermes: ungated turns must use a no-terminal/no-file toolset, `--ignore-user-config`, and `HERMES_YOLO_MODE` unset in the child; otherwise refuse.
- OpenClaw: auth vs `--config-override` resolved; no credential-isolation claim; refuse if deny-exec is unproven.
- Explicit per-provider timeouts, each 300 s or less.
- NemoClaw: version pin, update policy, fail-closed readiness check, kill switch.
- Containers: rootless Podman preferred over the docker group.
- Reword P2-1.

**10:38 rulings, applied in sections 3-5:**

- N-1: per-job Hermes gateway with a private `HERMES_HOME`, or refuse.
- N-2: the exact #336 parts are listed in section 4.
- N-3: GUARD #335 is the target shell path; native-shell paths are INTERIM.
- N-4: OpenClaw and NemoClaw use a tool allowlist.
- N-5 to N-9 are fixed.

## 1. Findings

| Provider | Identity (official) | Ambiguity | Headless method | Auth | Output | Approval / shell | License | Local / hosted | Fit |
|---|---|---|---|---|---|---|---|---|---|
| Hermes | **Hermes Agent** CLI, `NousResearch/hermes-agent` [V1] | Yes: Hermes Agent CLI vs Hermes 4 model family (weights on HF, served via OpenAI-compatible Nous Portal API) [V5] vs NemoClaw's Hermes sandbox [V12] | `hermes chat -q "<p>" -Q` answers and exits; `--oneshot` implied on non-TTY [V2] | `hermes setup` / `hermes model`; Nous Portal OAuth or provider keys; `--provider custom --base-url --api-key` [V2] | `--format stream-json` JSONL, terminal `result` record carries `text`, `exit_code` [V2] | `--yolo` skips approvals [V2]; `approvals.single_query_mode` default `deny` for `-q` [V3]; **`-z` forces `HERMES_YOLO_MODE=1`** [V4]; `-t/--toolsets <csv>`, `--ignore-user-config` [V2]; toolset `safe` = no file/terminal/code [V21]; terminal tool runs non-dangerous commands without a prompt [V3] | MIT [V1]; Hermes 4 weights `llama3` [V5] | Local CLI (Python), any hosted or local model | **Fair.** One argv and a JSON stream, but `hermes chat` is a gateway client [V22], so Hush must own a per-job gateway (section 3). |
| OpenClaw | `openclaw/openclaw`, formerly Warelay, CLAWDIS, Clawdbot, Moltbot (renamed 2026-01-30) [V6] | Low. One project. NemoClaw (NVIDIA) wraps it [V12] | `openclaw agent exec "<p>" --cwd <dir> --timeout <s> --json` runs one embedded turn with no Gateway [V7] | Ambient config and stored creds by default; `--isolated` drops ambient config only; `--auth-env-only` cannot be combined with `--config`/`--config-override` [V7] | `--json` stable envelope `{ok,status,final,...}`; exit 0/1/2(timeout) [V7] | `agent exec` defaults: sandbox off, `coding` profile, **full exec policy** [V7]; `--config-override '{"tools":{"deny":["exec"]}}'` [V7]; `tools.exec.mode deny/allowlist/ask/auto/full` [V8] | MIT [V6] | Local Node CLI plus optional Gateway; any model | **Good.** Shell on by default, so ungated turns need the tool allowlist (section 3) or are refused. |
| NVIDIA NeMo → **NemoClaw** | `NVIDIA/NemoClaw`: a reference stack that runs OpenClaw (default), Hermes or LangChain Deep Agents Code inside NVIDIA OpenShell sandboxes [V12]. Other NeMo products considered and not chosen: NIM API [V9], NeMo Agent Toolkit `nat` [V10], NeMo framework and Guardrails [V11] | **Resolved** by the CoS ruling of 09:48 | OpenClaw sandbox: `nemoclaw <sandbox> agent --agent main --json -m "<p>" --timeout <s>` forwards to `openclaw agent` in the sandbox [V15]. Hermes sandbox: the `agent` wrapper refuses; use the in-sandbox OpenAI-compatible API on `127.0.0.1:8642-8652` with `gateway-token` [V16] | `nemoclaw onboard` (the non-interactive examples pass `--yes-i-accept-third-party-software`). Provider creds are registered with the OpenShell gateway and never written to host disk (NVIDIA Endpoints, OpenAI, Anthropic, ...) [V15] | `--json` OpenClaw envelope; the wrapper exits 1 on `incomplete_turn` or timeout [V15] | No per-turn approval flag in the wrapper (U). Shell runs **inside** the OpenShell sandbox. Network egress is set by policy tier (restricted/balanced/open/personal), and host mounts are read-only only [V15] | Apache-2.0 [V12]; **alpha**, best-effort support [V12]; tags v0.0.x (latest v0.0.135), package 0.1.0 [V17] | Local: Node 22.19+, Docker (default) or rootless Podman, OpenShell gateway/k3s, 8 GB RAM minimum, ~2.4 GB image [V18]; inference hosted or local | **Fair.** Reuses the OpenClaw (M2) and Hermes (M1) adapters behind one more wrapper. Heavy prerequisites; alpha. |
| NanoClaw | `nanocoai/nanoclaw` (`qwibitai/nanoclaw` redirects here) [V13] | Low. One project | Daemon plus `pnpm run chat <msg>` over the `data/cli.sock` Unix socket (JSON lines `{"text":...}`) [V14] | Credential gateway with an Anthropic credential; Docker [V13] | Plain reply text. Exits after 2 s of silence; exit 3 if no reply within 120 s [V14] | **No per-turn approval or tool flag found (U).** Bash always available inside the per-session container [V13] | MIT [V13] | Local daemon plus Docker; Anthropic Agents SDK | **Poor.** Daemon-owned and one client at a time (a second client supersedes the first) [V14]. Cannot run parallel Hush jobs. |

Sources: [V1] https://github.com/NousResearch/hermes-agent (README, LICENSE) ·
[V2] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/reference/cli-commands.md ·
[V3] https://hermes-agent.nousresearch.com/docs/user-guide/security ·
[V4] https://github.com/NousResearch/hermes-agent/blob/main/hermes_cli/oneshot.py (L276-277) ·
[V5] https://huggingface.co/NousResearch/Hermes-4-70B , https://portal.nousresearch.com/info ·
[V6] https://github.com/openclaw/openclaw , https://github.com/openclaw/openclaw/blob/main/docs/start/lore.md ·
[V7] https://docs.openclaw.ai/cli/agent · [V8] https://docs.openclaw.ai/tools/exec-approvals ·
[V9] https://docs.api.nvidia.com/nim/docs/api-quickstart , https://build.nvidia.com/llms.txt ·
[V10] https://github.com/NVIDIA/NeMo-Agent-Toolkit , https://docs.nvidia.com/nemo/agent-toolkit/latest/run-workflows/about-running-workflows.html ·
[V11] https://api.github.com/repos/NVIDIA/NeMo , https://api.github.com/repos/NVIDIA/NeMo-Guardrails (both 301 to NVIDIA-NeMo/*) ·
[V12] https://github.com/NVIDIA/NemoClaw (README, LICENSE) · [V13] https://github.com/nanocoai/nanoclaw · [V14] https://docs.nanoclaw.dev/channels/cli ·
[V15] https://docs.nvidia.com/nemoclaw/latest/user-guide/openclaw/reference/commands · [V16] https://docs.nvidia.com/nemoclaw/latest/user-guide/hermes/reference/commands (`nemohermes <name> agent`) ·
[V17] https://api.github.com/repos/NVIDIA/NemoClaw/tags , https://github.com/NVIDIA/NemoClaw/blob/main/package.json · [V18] https://docs.nvidia.com/nemoclaw/latest/user-guide/openclaw/get-started/prerequisites · [V19] https://docs.nanoclaw.dev/concepts/security ·
[V20] https://docs.openclaw.ai/tools (L87-88 tool names) · [V21] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/reference/toolsets-reference.md (`safe`) · [V21b] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/tools.md (terminal backends) ·
[V22] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/cli.md ("The classic CLI is a gateway client") · [V23] https://github.com/NousResearch/hermes-agent/blob/main/hermes_cli/gateway_client.py (`HERMES_TUI_GATEWAY_URL`, `ensure_gateway_runtime(..., idle_exit=True)`) ·
[V24] https://docs.openclaw.ai/gateway/config-tools/tool-policy (profiles, groups, `alsoAllow`, deny wins)

## 2. How providers are wired today (adapter contract)

1. **Id and registry.** Wire ids are in `hush-c/include/hush_roster.h:42-53`. The meta table (`id, label, family, host, binary, caps, flags`) is `hush-c/src/hush_provider.c:48-91`, sized by `HUSH_PROVIDER_COUNT = 12` (`hush-c/include/hush_provider.h:19`). UI wire ids and drawer family rows are in `UI_SPEC.md:327-331` and `UI_SPEC.md:579-589`.
2. **Readiness.** A binary must be on PATH (`hush_provider_has_binary`, `hush_provider.c:589`). Home detection checks `~/.grok/auth.json`, `~/.codex/auth.json|config.toml` and `~/.copilot/config.json` (`hush_provider.c:545-587`). Hush never writes provider homes (`UI_SPEC.md:581-582`). `hush_agent_pick_provider` picks the first ready ranked provider (`hush-c/src/hush_agent.c:735-750`), and `hush_agent_can_start` wraps it (`:752-757`).
3. **Dispatch gate.** `hush_agent_begin_work` holds the turn for the owner under Approve every action before any runtime starts (`hush-c/src/agent_dispatch.c:1362-1364`, `agent_approve.c:40-71`). Then the can-start check runs (`agent_dispatch.c:1371`).
4. **Person gate (#311 A3 / #322 / #323).** `job->person_gate = in->approved || hush_agent_owner_asked(in)` (`hush-c/src/agent_prompt.c:152-153`). `owner_asked` means the owner's note, or a robot note in a thread the owner opened (`agent_approve.c:140-154`). `hush_agent_apply_project_cwd` sets `project_tools = 1` only when person_gate holds **and** the thread is bound to a milestone project dir (`hush-c/src/agent_team.c:321-345`). **Target rule (not current behaviour):** `project_tools` should be the only switch that grants shell or auto-approve. Today it is not: Copilot always gets `--allow-all-tools` (`agent_process.c:40`, `:457`), Goose runs with no mode, so `GOOSE_MODE=auto` (`:556-559`), and Grok always gets `--always-approve` (`:480`), though Grok's shell tool is denied by both denylists (`:26-29`). Copilot and Goose are fixed in #333.
5. **Spawn.** `hush_agent_spawn_grok` forks a worker (`hush-c/src/agent_process.c:588-615`). The worker forks the CLI with stdout on a pipe and stderr on /dev/null (`:279-290`) and dispatches on `job->provider` (`:291-308`). API-family ids stream through `hush_inference_stream` instead (`:291-293`, `:330-359`; endpoint `/v1/chat/completions` built at `hush-c/src/hush_inference.c:165-186`).
6. **Prompt passing.** Grok takes `-p note`, `--system-prompt-override prompt` and `--rules` (`agent_process.c:468-500`). Every other CLI gets one combined `prompt\nrules\nnote` (`hush_agent_build_combined`, `:502-514`) as a single argv element. Nothing goes on stdin.
7. **Per-provider argv and approval mapping.**
   - Grok Build: `--always-approve` plus a `--disallowed-tools` denylist. The denylist drops only the file tools when `project_tools` is set, and `--max-turns` is 2 or 8 (`:417-429`, `:473-497`).
   - Codex: `exec --cd <cwd> --skip-git-repo-check --sandbox read-only` (`:529-545`).
   - Cline: the pure builder `hush_agent_cline_argv`: `--json --cwd --auto-approve <true|false from project_tools> --timeout <300|80>` (`:361-403`; struct at `hush_agent_internal.h:567-584`).
   - Goose: `chdir(cwd)`, then `run --text <combined>`, with no approval flag (`:547-563`).
   - Copilot: `-p <combined> --allow-all-tools [-C cwd]`, unconditional (`:40`, `:440-466`).
8. **Output parse.** Cline keeps the last `say|ask` record whose subtype is `text|completion_result` and that is not `partial` (`agent_process.c:93-127`). Every other CLI returns raw stdout, truncated to the event cap (`:209-225`). Failure goes out as `HUSH_JOB_ERR:<reason>` (`:228-240`).
9. **Clock.** The job wall clock is 300 s with project tools, else 90 s (`hush-c/include/hush_agent.h:14`, `:17`; `hush_agent_budget_seconds`, `agent_dispatch.c:475-479`). Cline's own `--timeout` is 300 or 80 s (`hush_agent.h:19`). `SIGTERM` is followed by `SIGKILL` after 3 s (`hush_agent_internal.h:30`).
10. **Hermetic tests.** `make test` puts refusing sentinels (exit 97) ahead of `goose grok codex copilot cline ollama` (`hush-c/Makefile:178-195`), and `tests/check_hermetic_path.sh:14-24` asserts it. `tests/test_cline_gate.c` drives the real mention path. A stub `cline` first on PATH records its argv (`:85-100`), under a scratch HOME/HUSH_HOME/CLINE_DATA_DIR (`:103-125`). It covers 4 cases: owner auto, guest auto, guest in an owner thread, guest plus owner Yes (`:251-305`). CI runs `./configure && make && make test` (`.github/workflows/ci.yml:19-26`).

## 3. Per-provider mapping, gaps, risks

### Hermes Agent (`hermes`), rulings Q1, 10:12 and 10:38

- **The CLI is a gateway client (V).** `hermes chat` "no longer runs the agent inside your terminal process. It discovers or starts the profile's gateway (`hermes gateway ensure`) ... Closing the terminal detaches ... it does not cancel the turn" [V22]. `--toolsets`, `--yolo`, `--ignore-rules` and `--max-turns` are frozen into the session [V22].
  - So tools run in the **gateway** process, not in the Hush child.
  - An operator gateway (Desktop, `hermes gateway start`) would run Hush turns outside any #336 sandbox, and turns would outlive the Hush clock.
- **Per-job gateway (10:38 N-1), required for every Hermes turn:**
  1. **Private home.** Hush creates a fresh 0700 `HERMES_HOME` under the job's Hush-owned HOME. `HERMES_HOME` selects the home and its gateway [V2].
     - Hush writes that home's `.env` itself, with the provider key only.
     - Hush never points it at `~/.hermes`.
  2. **Child env.** The child env comes from the #336 allowlist (section 4). All `HERMES_*` are dropped, then `HERMES_HOME` is set. In particular, `HERMES_TUI_GATEWAY_URL` (selects an explicit gateway [V23]) and `HERMES_YOLO_MODE` [V4] are unset.
  3. **Gateway start.** Inside the #336 sandbox, Hush spawns `hermes gateway run --external-supervisor` as its own child, in the job's process group. `run` is the foreground gateway, and `--external-supervisor` means a wrapper owns it [V2].
     - Hush waits for `hermes gateway ensure --json --timeout 15` to exit 0. Exit 0 means ready; other codes are a refusal or a deadline [V2].
     - Only then does Hush run `hermes chat`.
  4. **Gateway stop.** At turn end, on the Hush timeout, and on `/stop`, Hush SIGTERMs and then SIGKILLs **only the PIDs it spawned** (gateway and chat process group), then deletes the private home.
- **Ungated argv (`project_tools == 0`):** `hermes chat -q <combined> --format stream-json --safe-mode -t safe --model <m> --provider <p> --max-turns 2`, with `chdir(job->cwd)`.
  - `--safe-mode` implies `--ignore-user-config` and `--ignore-rules`, and also disables plugins, shell hooks and MCP [V2].
  - `-t safe` is "No file writes, no terminal, no code execution" [V21].
  - If `--safe-mode` proves unsuitable for integration, the fallback is `--ignore-user-config --ignore-rules -t safe` (CoS 10:12 minimum).
- **Gated argv (`project_tools == 1`), INTERIM:** `hermes chat -q <combined> --format stream-json --ignore-user-config --ignore-rules --model <m> --provider <p> --max-turns 8 --yolo`.
  - This is a native host shell inside the #336 sandbox.
  - It is allowed only after all #336 parts in section 4 are merged.
  - **It retires when GUARD G12 lands for Hermes** (#335 §2/§8: ACP with approvals `manual`; see section 4).
  - No container terminal backend: GUARD says "Never a container backend without GUARD" (#335 §2).
- **Never** `-z` (hard-sets YOLO [V4]) and never ungated `--yolo`.
- **Fail closed:** `tests/test_hermes_gate.c` must prove all of the following, with reverse mutants:
  - (a) ungated argv has `--safe-mode`/`-t safe` and lacks `--yolo`, `-z` and any terminal/file toolset;
  - (b) the child env lacks `HERMES_YOLO_MODE` and `HERMES_TUI_GATEWAY_URL` (both planted in the parent) and has the private `HERMES_HOME`;
  - (c) the stub gateway is started under the private home, and a planted "operator gateway" under the parent's `HERMES_HOME` is never contacted;
  - (d) the gateway PID is gone after a normal end and after a timeout.
  - A real-binary check in the M1 PR must show:
    - `-t safe` **replaces** the default toolsets (N-9; the docs only say "Enable" [V2]);
    - a turn under the private home does not attach to a running operator gateway;
    - killing the job's gateway ends the turn.
  - If any proof or check fails, **every Hermes turn is refused**: `HUSH_JOB_ERR:Hermes needs a Hush-owned gateway for this turn; not available`.
- **Parse:** `result.text` of the last `type:"result"` record; fail if `exit_code != 0` [V2]. Exit 3 means the queued input was not run [V22].
- **Gaps (U):**
  - Whether a home outside `~/.hermes` can be folded into the "one host gateway" by `hermes gateway migrate --multiplex` [V2].
  - Whether two private gateways conflict on port or socket.
  - Whether the dotenv loader reads `$HERMES_HOME/.env` (I) and not a fixed `~/.hermes/.env` (N-5). The stub (b) and the real check cover this.

### OpenClaw (`openclaw`), rulings Q4, 10:12 and 10:38

- **argv:** `openclaw agent exec --isolated --json --code-mode direct --cwd <cwd> --timeout <T> --config-override <policy> <combined>`.
  - `--code-mode direct` disables Code Mode, so tools cannot be reached through the code-mode bridge [V7].
  - It runs embedded in the Hush child, with no Gateway [V7].
- **Auth (resolved, V):** `--auth-env-only` "loads no config at all, and pairing it with `--config` or `--config-override` is rejected" [V7]. Hush therefore uses the default `--no-auth-env-only`, i.e. stored and external CLI credentials.
  - `--isolated` only means "ignore the ambient config and use only exec defaults" [V7]. It is not credential isolation.
  - Overrides apply after the `--isolated` base [V7].
- **Ungated allowlist (10:38 N-4):**
  - Policy: `{"tools":{"profile":"minimal","alsoAllow":["read","web_search","web_fetch"],"deny":["gateway","write","edit","apply_patch","group:runtime","group:sessions","group:automation","group:nodes","group:ui","group:plugins"]}}`.
  - `tools.profile` `minimal` (`session_status`, `gateway` update-only) is the base allowlist; `alsoAllow` adds the read and web tools; `deny` wins [V24].
  - Group names and members are in [V24]. `read` is restricted to `--cwd` [V7].
  - `allow` and `alsoAllow` cannot share a scope [V24], so Hush uses only `profile` + `alsoAllow` + `deny`.
- **Gated policy (`project_tools == 1`), INTERIM:** `{"tools":{"profile":"minimal","alsoAllow":["group:fs","group:runtime","group:web"],"deny":["gateway","group:sessions","group:automation","group:nodes","group:ui","group:plugins"]}}` [V24].
  - This is a native host shell, allowed only after all #336 parts in section 4 are merged.
  - **It retires when GUARD G9/G12 lands for OpenClaw**: native shell denied, shell only through the Hush MCP `shell` tool (#335 §2/§8).
- **Fail closed:** the M2 stub test must assert the exact `--config-override` JSON and `--code-mode direct` on every case, with reverse mutants (missing deny, `coding` profile, extra `alsoAllow`).
  - The M2 PR must record one real-binary check that the ungated run offers only the allowlist: an adversarial prompt asks for each denied tool, and `meta.toolSummary.tools` [V7] must stay within `read, web_search, web_fetch`.
  - Until both exist, ungated turns are **refused**: `HUSH_JOB_ERR:OpenClaw tool allowlist not proven; a person must ask for this turn`.
- **Parse:** `final` when `ok:true`; exit 2 means timeout [V7].
- **Timeout (N-7):** `--timeout` applies per model attempt, with a fresh budget per configured fallback [V7]. `--isolated` has no ambient config, and Hush's override adds no model fallbacks, so one budget applies (I).
- **Risks:**
  - `AGENTS.md` is still injected [V7].
  - SIGTERM exits 143 after owned cleanup [V7], but Hush's SIGKILL 3 s later may leave temp state behind (I).
  - Do not use `--state-dir` (it takes an exclusive lock [V7]).

### NVIDIA NemoClaw (`nemoclaw`), rulings Q2 and 10:12

- **Identity and maturity [V12, V17]:**
  - Official repo `NVIDIA/NemoClaw`, Apache-2.0.
  - The README calls it an "alpha project" with best-effort maintainer review.
  - Tags run v0.0.x (latest v0.0.135, about 2 a week). The only GitHub release is a CI prerelease, `ci-native-podman-e2e-2026-10-01`.
- **Prerequisites [V18]:**
  - Node 22.19+ and npm 10+.
  - An OpenShell gateway: `nemoclaw onboard` creates it and the sandbox.
  - 4 vCPU, 8 GB RAM and 20 GB disk minimum.
  - A container runtime: **rootless Podman preferred** (`NEMOCLAW_GATEWAY_RUNTIME=podman`, qualified on native Linux/WSL2 [V18]). Docker is the default, but it needs the `docker` group, which is root-equivalent [V18]. See "Containers" below.
- **Inner agents [V15]:** `openclaw` (default), `hermes`, `langchain-deepagents-code`.
- **Headless turn, OpenClaw sandbox [V15]:**
  - Command: `nemoclaw <sb> agent --timeout <T> --session-id hush-<thread> --json -m <combined>`.
  - `--timeout` comes first, because an unrecognised option before it leaves the host wait unbounded [V15].
  - `--session-id` keeps Hush threads from sharing the main session.
  - The wrapper exits 1 on `incomplete_turn`, abandoned or timeout. The envelope is `{payloads, meta}` or `{status, result:{payloads, meta}}` [V15].
- **Headless turn, Hermes sandbox [V16]:** the `agent` wrapper refuses. The alternative is the in-sandbox OpenAI-compatible API on `127.0.0.1:<8642-8652>` with Bearer `nemohermes <name> gateway-token --quiet`.
- **Reuse:**
  - The OpenClaw path reuses the M2 allowlist policy and parser.
  - The Hermes path would be an API-family call (`hush_inference_stream`, `hush_inference.c:556`), but the agent behind it still runs tools.
- **Isolation [V15]:**
  - Tools run in the OpenShell sandbox.
  - Egress follows the policy tier (`balanced` default). Even `personal` keeps "loopback, and link-local ranges ... blocked".
  - `--host-mount` is read-only.
  - Credentials are kept by the OpenShell gateway, per the vendor claim (not tested).
- **Gate (#322):**
  - If `project_tools == 1`: run (INTERIM until GUARD G12, section 4).
  - Otherwise, OpenClaw sandbox: forward the **M2 allowlist** policy (10:38 N-4) only if a stub proves it and a real check shows it is honoured; else **refuse**.
    - Candidate path (U): `nemoclaw <sb> exec --timeout <T+30> --no-stdin -- openclaw agent exec --isolated --json --code-mode direct --config-override <allowlist> --timeout <T> <combined>`. `exec` forwards argv verbatim [V15].
    - Whether `agent exec` gets inference inside the sandbox is U.
  - Otherwise, Hermes sandbox: **always refuse**.
  - Refusal reason: `HUSH_JOB_ERR:NemoClaw runs tools in its sandbox; a person must ask for this turn`.
- **Version pin and update policy (10:12):**
  - Hush carries a compile-time allowlist of tested NemoClaw versions, starting with one pinned tag. Operators install with `NEMOCLAW_INSTALL_TAG=<pinned tag>` [V15].
  - Hush never runs `nemoclaw update`. The upstream maintained installer follows the admin-promoted `lkg` tag, which can differ from the newest tag [V15].
  - An operator may run `nemoclaw update --check` [V15]. A pin bump is a Hush PR that re-runs the M3 stub fixtures against the new envelope.
- **Fail-closed readiness (10:12, N-8):** NemoClaw is ready only if all of these hold:
  - `nemoclaw --version` [V15] is on the allowlist;
  - `nemoclaw sandbox status <sb> --json` exits 0, with `agent` (always a string [V15]) equal to the expected inner agent. This canonical form returns `found: false` for an unknown sandbox [V15];
  - the kill switch is off.
  - Status runs inference probes that cost provider tokens [V15], so Hush caches the result with a TTL (300 s, I). The kill switch is still checked before every spawn.
  - Any other result, a timeout, or unparsable output means not ready, and the turn is refused with `HUSH_JOB_ERR:NemoClaw not ready (<reason>)`.
  - An unknown reply envelope fails the turn and does not fall back to raw text.
- **Kill switch (10:12, I):** an operator setting such as `providers.nemoclaw.disabled` (name chosen in M3). It marks NemoClaw not ready immediately and is checked before every spawn.
- **Gaps (U):**
  - cwd mapping (read-only mounts; `upload`/`download`).
  - Allowlist passthrough (`openclaw agent` or `exec -- openclaw agent exec`).
  - Hermes API tool limits.
  - Concurrency per sandbox.
  - Whether Hush's SIGKILL 3 s after SIGTERM orphans the in-sandbox turn.

### NanoClaw (`nanoclaw`), ruling Q3: GO behind the person gate

- **Integration (I):** speak the `data/cli.sock` JSON-line protocol (`{"text":...}`) directly from C [V14].
- **Gate:** bash is always available in its container [V13]. So `project_tools == 1` is required. The gated container bash is **INTERIM** until GUARD G12 (section 4).
  - Otherwise refuse before connecting: `HUSH_JOB_ERR:NanoClaw always has a shell in its container; a person must ask for this turn`.
  - The refusal goes through `hush_agent_worker_err` (`agent_process.c:228-240`).
- **Isolation [V13, V19]:**
  - One container per session, with `--cap-drop=ALL`, `no-new-privileges` and a non-root agent.
  - Only explicit mounts: no host home, `.ssh` or Docker socket. Extra mounts are checked against `~/.config/nanoclaw/mount-allowlist`.
  - Credentials go through the credential gateway.
  - Egress lockdown is **off by default**. When on, the container is on a Docker `--internal` network and spawn fails if lockdown cannot be set up. Hush docs require turning it on.
  - The operator owns the daemon; Hush never starts or stops it.
- **Risks:**
  - One client at a time; a second client supersedes the first [V14]. Hush serialises NanoClaw turns and refuses extras as "busy" (I).
  - Any same-uid process can drive or supersede `data/cli.sock`, so a host-shell child could hijack Hush's turn (I; another reason for #336 first).
  - The 2 s-silence exit can truncate replies [V14].
  - Rootless Podman support is U, which matters under the docker-group rule (Containers).

### Copilot / Goose: tracked in #333 (P1, Floor)

Copilot's `--allow-all-tools` (`agent_process.c:40,457`) and Goose's `run` with no approval flag (`:556-559`) ignore `project_tools`. Goose `GOOSE_MODE` defaults to `auto` (V: https://github.com/block/goose/blob/main/documentation/docs/guides/config-files.md L52). Fix and tests are in #333.

### Timeouts (10:12): every value is 300 s or less, and below the Hush clock

The Hush clock (N-6, corrected) is 300 s for a project job, else **90 s** (`hush_agent.h:14`, `:17`; `hush_agent_budget_seconds`, `agent_dispatch.c:475-479`). 80 s is only Cline's own `--timeout` (`hush_agent.h:19`). SIGKILL follows SIGTERM after 3 s (`hush_agent_internal.h:30`).

| Provider | Gated | Ungated | Mechanism |
|---|---|---|---|
| Hermes | 270 s | 60 s | No CLI turn-timeout flag found [V2] (U). Hush kills its own gateway and chat PIDs at the deadline (section 3); `gateway ensure --timeout 15` counts within it |
| OpenClaw | `--timeout 270` | `--timeout 60` | Per model attempt; no fallbacks [V7] |
| NemoClaw | `--timeout 240` | `--timeout 50` if the allowlist is proven, else refused | Wrapper wait is `T+30` [V15]: 270 < 300, 80 < 90 |
| NanoClaw | 270 s socket read deadline | refused | `pnpm run chat` gives up at 120 s [V14]; daemon-side limit is U |

### Containers (10:12): rootless Podman preferred

- `docker` group membership is root-equivalent [V18].
- **Rule:** the Hush relay uid must not be in the `docker` group. Prefer rootless Podman:
  - NemoClaw supports it (`NEMOCLAW_GATEWAY_RUNTIME=podman` [V18]).
  - NanoClaw Podman support is U. If it needs Docker, it runs under a separate operator uid, or it is not enabled.
- Hermes container terminal backends are **not** used: they skip approvals (#335 §2).
- **Required in docs:**
  - never mount `$HOME` or `$HUSH_HOME` (NemoClaw `--host-mount`, NanoClaw mount allowlist);
  - never use host networking;
  - enable NanoClaw egress lockdown.

## 4. Gate rule for every new adapter (#322/#323, #333, #336, GUARD #335)

> Any flag, env var or config that auto-approves tools or grants shell/file write is emitted **only** when `job->project_tools == 1`. That value is set solely by `hush_agent_apply_project_cwd` behind `person_gate`. The default mode is the least-privileged one the tool offers: a **tool allowlist**, proven by a stub test with reverse mutants plus a recorded real-binary check. If that cannot be proven, the adapter refuses without `project_tools`, with a `HUSH_JOB_ERR:` reason.

**#336 parts required before ANY host-shell adapter (gated Hermes, gated OpenClaw) merges (CoS 10:38 N-2):**

1. **Set-Cookie bootstrap closed.** An unauthenticated loopback reply must no longer carry `hush_session=<token>` (`hush_http.c:286-290`, `:732-738`; #336 §1, open Q2).
2. **Forged owner notes closed.** An owner-stamped write (`api_channels.c:102` → `owner_asked` `agent_approve.c:140-154` → `person_gate` `agent_prompt.c:153`) needs the browser-session proof, not only the token (#336 open Q3; GUARD #335 §6).
   - Related: a robot note in an owner-opened thread also counts as an owner ask (`agent_approve.c:140-154`). This is noted for KEYS #334 and GUARD #335.
3. **Relay-port / loopback deny.** Agent children, and the Hermes job gateway, cannot reach the relay port on loopback (GUARD #335 §6).
4. **Secret env scrub, as an allowlist.**
   - The child env is built from an allowlist, never inherited (`agent_process.c:65-74` today).
   - It drops `HUSH_KEY_PASS`, `HUSH_HOME`, `HUSH_CONFIG_DIR`, `HUSH_OP_*`, every other `HUSH_*`, and any URL carrying a `?k=` token (`hush_http.c:529-554` carriers).
   - `$HUSH_HOME` is hidden with Landlock or bwrap (#336 a; PLAN_KEYS #334 T3).
   - The same allowlist applies to ungated turns of every new adapter.
5. **Approval UI nonce (#336 b) plus Origin/CSRF (#336 c).**
   - A `Yes`/`No` or `approval_mode` change needs the `hush_ui` session proof.
   - **Strict fallback:** if the nonce or the sandbox is unavailable, `approval_mode` stays at Approve every action and shell-capable turns are refused (#336 Q1).

#336's current fix plan (c, b, a) does not yet include items 1-3 (they are open questions or out of scope there). They must be added to #336 or tracked in follow-up issues before M1/M2 gated grants.

**Target shell path: GUARD #335 (CoS 10:38 N-3).** This plan adopts GUARD (PR #335, `docs/guard-plan.md` §2 table, §6, §8 G9/G12; referenced by PR and section because #335 is being revised) as the **target** for every shell grant.

- OpenClaw, NanoClaw and NemoClaw: native shell denied; shell only through the **Hush MCP `shell` tool** (M).
- Hermes: #335 currently assigns **ACP with approvals `manual`** (P), with M as the fallback. **Open for CoS:** the 10:38 wording says "Hush MCP shell tool" for all; this plan follows #335 until CoS or #335 says otherwise.

**INTERIM native-shell paths:**

| Path | Allowed when | Retired when |
|---|---|---|
| Hermes gated `--yolo` (job gateway, #336 sandbox) | #336 parts 1-5 merged, and the M1 proofs pass | GUARD G12 (Hermes P) merges |
| OpenClaw gated `group:runtime` + `group:fs` | #336 parts 1-5 merged, and the M2 proofs pass | GUARD G9/G12 (OpenClaw M) merges |
| NanoClaw gated container bash | #336 parts 1-5 merged; container rules (section 3) | GUARD G12 (NanoClaw M) merges, or the NanoClaw seam is UNKNOWN, so Bash is denied (#335 §2) |
| NemoClaw gated sandbox shell | #336 parts 1-5 merged; readiness and pin checks | GUARD G12 (NemoClaw M) merges |

## 5. Milestones (one small DRAFT PR each, CoS order 10:12)

| Order | PR | Scope | Open (U) |
|---|---|---|---|
| 0 | **#336 fix** | All five parts in section 4 (Set-Cookie bootstrap, forged owner notes, relay-port deny, env allowlist scrub, UI nonce + Origin/CSRF + strict fallback). Blocks every host-shell grant | Parts 1-3 not yet in the #336 fix plan |
| 1 | **#333 fix** (Floor) | Gate Copilot `--allow-all-tools` and Goose `GOOSE_MODE` on `project_tools` | Copilot headless without the flag |
| 2 | M1 `gb/provider-hermes` | Per-job private `HERMES_HOME` + Hush-owned gateway lifecycle. Ungated `--safe-mode -t safe`. Gated `--yolo` INTERIM. Env allowlist. Refuse unless proofs (a)-(d) and the real check pass. 270/60 s | `-t` semantics; multiplex fold; port conflicts |
| 3 | M2 `gb/provider-openclaw` | `agent exec --isolated --code-mode direct` + allowlist policy. Stored-creds auth. Refuse until stub + real check pass. 270/60 s | Offered-tool enumeration method |
| 4 | M4 `gb/provider-nanoclaw` | Socket client, refuse unless `project_tools`, one turn in flight, 270 s, container rules; gated bash INTERIM | Podman; project mount; daemon limit |
| 5 | M3 `gb/provider-nemoclaw` | Version allowlist, cached fail-closed readiness, kill switch, Podman preferred, `--session-id`; allowlist passthrough or refuse; gated INTERIM | Passthrough; cwd mapping; concurrency |
| later | GUARD G9/G12 (#335) | Retire the INTERIM native-shell paths | Per #335 |

**Each PR must include:**

- Stub tests on the `tests/test_cline_gate.c` model, named `tests/test_<id>_gate.c`. A stub binary or socket goes first on PATH under a scratch HOME/HUSH_HOME.
- Argv/env/refusal assertions for owner, guest, guest-in-owner-thread and guest+Yes, plus reverse mutants and a parse test.
- The new binary added to the sentinel list (`hush-c/Makefile:193`, `hush-c/tests/check_hermetic_path.sh:14`), so the canary stays at 0.
- `HUSH_PROVIDER_COUNT` and the UI_SPEC wire ids updated.
- CI green on the exact head.
- Draft only, `Refs #322`.

## 6. Questions for CoS

- Q1-Q5 were resolved at 09:45/09:48 ET. The 10:12 and 10:38 rulings are applied.
- **Open:** Hermes target mechanism. #335 says ACP (P), CoS 10:38 says Hush MCP shell (section 4).

## 7. Gauge reviews: disposition

**158c050b (FAIL, 10:10 ET).**

| Finding | Where addressed |
|---|---|
| P1-1 #336 not gating host shells | Section 4 (#336 parts 1-5); section 5 row 0; Hermes and OpenClaw INTERIM bullets |
| P1-2 Hermes ungated not fail-closed | Hermes section: ungated argv, env, fail-closed proofs |
| P2-1 "only switch" overclaim | Section 2 item 4 |
| P2-2 OpenClaw auth/deny/`--isolated` | OpenClaw section; section 0 Q4 row |
| P2-3 timeouts | Timeouts table |
| P2-4 NemoClaw pin/update/fallback | NemoClaw section |
| P2-5 docker group | Containers |
| P3-2..P3-7 | NemoClaw headless and identity; Containers; NanoClaw risks |

**4acd4c3c (FAIL r2, 10:38 ET), CoS 10:38.**

| Finding | Where addressed |
|---|---|
| N-1 Hermes gateway outside the child/clock (P1) | Hermes "gateway client" and "Per-job gateway" bullets; proofs (c)(d); refuse rule; Timeouts Hermes row |
| N-2 "#336 fixed" undefined (P2) | Section 4 "#336 parts required" 1-5; section 5 row 0 |
| N-3 GUARD #335 not referenced (P2) | Section 4 "Target shell path" and INTERIM table; Hermes/OpenClaw INTERIM bullets; Containers (no Hermes container backend) |
| N-4 OpenClaw denylist vs profile (P2) | OpenClaw ungated/gated allowlist policy and fail-closed check; NemoClaw gate |
| N-5 `.env` reloads `HERMES_YOLO_MODE` (P3) | Hermes private home writes its own `.env`; proof (b); gaps |
| N-6 clock misquoted (P3) | Section 2 item 9; Timeouts intro |
| N-7 OpenClaw per-attempt timeout (P3) | OpenClaw "Timeout" and "Risks" bullets |
| N-8 NemoClaw readiness details (P3) | NemoClaw readiness: `sandbox status --json`, cached TTL, `agent` field [V15] |
| N-9 Hermes `-t` semantics; `--safe-mode` (P3) | Hermes ungated argv (`--safe-mode`); the real check is a refuse condition |
