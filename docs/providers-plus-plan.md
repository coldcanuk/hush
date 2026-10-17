# PROVIDERS+ phase 1: research and plan (Hermes, OpenClaw, NVIDIA NemoClaw, NanoClaw)

Status: plan only. This PR adds no product code. Base: `main` @ `13511bd48b47` (#323).
Refs #322, #311 (person gate), #333 (Copilot/Goose gate fix). Researched 2026-10-17 (America/Toronto). CoS rulings added 09:45 and 09:48 ET.

Tags: **V** = VERIFIED_EXTERNAL (URL given), **I** = INFERRED, **U** = UNKNOWN.

## 0. CoS rulings (2026-10-17)

| Q | Ruling | Effect on this plan |
|---|---|---|
| Q1 Hermes | **Hermes Agent CLI** | M1 targets `hermes`. The Hermes 4 model family is out of scope; it stays reachable through `custom`. |
| Q2 NVIDIA | **NemoClaw**, `NVIDIA/NemoClaw` (Chuck, 09:48) | M3 targets NemoClaw (section 3). |
| Q3 NanoClaw | **GO**, only behind the #322 person gate | Turns without the gate are refused with a clear reason; Docker isolation documented (section 3). |
| Q4 OpenClaw | **`--isolated`** | `agent exec` ignores the operator's ambient config and credentials. |
| Q5 Copilot/Goose | **YES**: P1 issue **#333**, owned by Floor | Fix lands before M1. |

**Order:** #333 fix, then M1 Hermes, M2 OpenClaw, M4 NanoClaw, M3 NemoClaw.


## 1. Findings

| Provider | Identity (official) | Ambiguity | Headless method | Auth | Output | Approval / shell | License | Local / hosted | Fit |
|---|---|---|---|---|---|---|---|---|---|
| Hermes | **Hermes Agent** CLI, `NousResearch/hermes-agent` [V1] | Yes: Hermes Agent CLI vs Hermes 4 model family (weights on HF, served via OpenAI-compatible Nous Portal API) [V5] vs NemoClaw's Hermes sandbox [V12] | `hermes chat -q "<p>" -Q` answers and exits; `--oneshot` implied on non-TTY [V2] | `hermes setup` / `hermes model`; Nous Portal OAuth or provider keys; `--provider custom --base-url --api-key` [V2] | `--format stream-json` JSONL, terminal `result` record carries `text`, `exit_code` [V2] | `--yolo` skips approvals [V2]; `approvals.single_query_mode` default `deny` for `-q` [V3]; **`-z` forces `HERMES_YOLO_MODE=1`** [V4]; `-t/--toolsets <csv>` [V2]; terminal tool runs non-dangerous commands without a prompt [V3] | MIT [V1]; Hermes 4 weights `llama3` [V5] | Local CLI (Python), any hosted or local model | **Good.** Closest to Cline: one argv, JSON stream, explicit approval flag. |
| OpenClaw | `openclaw/openclaw`, formerly Warelay, CLAWDIS, Clawdbot, Moltbot (renamed 2026-01-30) [V6] | Low. One project. NemoClaw (NVIDIA) wraps it [V12] | `openclaw agent exec "<p>" --cwd <dir> --timeout <s> --json` runs one embedded turn with no Gateway [V7] | Ambient config and stored creds by default; `--isolated`, `--config <file>`, `--auth-env-only` [V7] | `--json` stable envelope `{ok,status,final,...}`; exit 0/1/2(timeout) [V7] | `agent exec` defaults: sandbox off, `coding` profile, **full exec policy** [V7]; `--config-override '{"tools":{"deny":["exec"]}}'` [V7]; `tools.exec.mode deny/allowlist/ask/auto/full` [V8] | MIT [V6] | Local Node CLI plus optional Gateway; any model | **Good.** Shell on by default, so the deny override is required unless a person gated the turn. |
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
[V17] https://api.github.com/repos/NVIDIA/NemoClaw/tags , https://github.com/NVIDIA/NemoClaw/blob/main/package.json · [V18] https://docs.nvidia.com/nemoclaw/latest/user-guide/openclaw/get-started/prerequisites · [V19] https://docs.nanoclaw.dev/concepts/security

## 2. How providers are wired today (adapter contract)

1. **Id and registry.** Wire ids are in `hush-c/include/hush_roster.h:42-53`. The meta table (`id, label, family, host, binary, caps, flags`) is `hush-c/src/hush_provider.c:48-91`, sized by `HUSH_PROVIDER_COUNT = 12` (`hush-c/include/hush_provider.h:19`). UI wire ids and drawer family rows are in `UI_SPEC.md:327-331` and `UI_SPEC.md:579-589`.
2. **Readiness.** A binary must be on PATH (`hush_provider_has_binary`, `hush_provider.c:589`). Home detection checks `~/.grok/auth.json`, `~/.codex/auth.json|config.toml` and `~/.copilot/config.json` (`hush_provider.c:545-587`). Hush never writes provider homes (`UI_SPEC.md:581-582`). `hush_agent_pick_provider` picks the first ready ranked provider (`hush-c/src/hush_agent.c:735-750`), and `hush_agent_can_start` wraps it (`:752-757`).
3. **Dispatch gate.** `hush_agent_begin_work` holds the turn for the owner under Approve every action before any runtime starts (`hush-c/src/agent_dispatch.c:1362-1364`, `agent_approve.c:40-71`). Then the can-start check runs (`agent_dispatch.c:1371`).
4. **Person gate (#311 A3 / #322 / #323).** `job->person_gate = in->approved || hush_agent_owner_asked(in)` (`hush-c/src/agent_prompt.c:152-153`). `owner_asked` means the owner's note, or a robot note in a thread the owner opened (`agent_approve.c:140-154`). `hush_agent_apply_project_cwd` sets `project_tools = 1` only when person_gate holds **and** the thread is bound to a milestone project dir (`hush-c/src/agent_team.c:321-345`). **`project_tools` is the only switch that may grant shell or auto-approve.**
5. **Spawn.** `hush_agent_spawn_grok` forks a worker (`hush-c/src/agent_process.c:588-615`). The worker forks the CLI with stdout on a pipe and stderr on /dev/null (`:279-290`) and dispatches on `job->provider` (`:291-308`). API-family ids stream through `hush_inference_stream` instead (`:291-293`, `:330-359`; endpoint `/v1/chat/completions` built at `hush-c/src/hush_inference.c:165-186`).
6. **Prompt passing.** Grok takes `-p note`, `--system-prompt-override prompt` and `--rules` (`agent_process.c:468-500`). Every other CLI gets one combined `prompt\nrules\nnote` (`hush_agent_build_combined`, `:502-514`) as a single argv element. Nothing goes on stdin.
7. **Per-provider argv and approval mapping.**
   - Grok Build: `--always-approve` plus a `--disallowed-tools` denylist. The denylist drops only the file tools when `project_tools` is set, and `--max-turns` is 2 or 8 (`:417-429`, `:473-497`).
   - Codex: `exec --cd <cwd> --skip-git-repo-check --sandbox read-only` (`:529-545`).
   - Cline: the pure builder `hush_agent_cline_argv`: `--json --cwd --auto-approve <true|false from project_tools> --timeout <300|80>` (`:361-403`; struct at `hush_agent_internal.h:567-584`).
   - Goose: `chdir(cwd)`, then `run --text <combined>`, with no approval flag (`:547-563`).
   - Copilot: `-p <combined> --allow-all-tools [-C cwd]`, unconditional (`:40`, `:440-466`).
8. **Output parse.** Cline keeps the last `say|ask` record whose subtype is `text|completion_result` and that is not `partial` (`agent_process.c:93-127`). Every other CLI returns raw stdout, truncated to the event cap (`:209-225`). Failure goes out as `HUSH_JOB_ERR:<reason>` (`:228-240`).
9. **Clock.** The job budget is 300 s with project tools, else 80 s for Cline (`hush-c/include/hush_agent.h:17-19`). `SIGTERM` is followed by `SIGKILL` after 3 s (`hush_agent_internal.h:30`).
10. **Hermetic tests.** `make test` puts refusing sentinels (exit 97) ahead of `goose grok codex copilot cline ollama` (`hush-c/Makefile:178-195`), and `tests/check_hermetic_path.sh:14-24` asserts it. `tests/test_cline_gate.c` drives the real mention path. A stub `cline` first on PATH records its argv (`:85-100`), under a scratch HOME/HUSH_HOME/CLINE_DATA_DIR (`:103-125`). It covers 4 cases: owner auto, guest auto, guest in an owner thread, guest plus owner Yes (`:251-305`). CI runs `./configure && make && make test` (`.github/workflows/ci.yml:19-26`).

## 3. Per-provider mapping, gaps, risks

### Hermes Agent (`hermes`)

- **argv (I):** `hermes chat -q <combined> --format stream-json --ignore-rules --max-turns <2|8> [-t <toolsets>] [--yolo]`, with `chdir(job->cwd)` as Goose does.
- **Gate:** add `--yolo` and the terminal toolset **only** when `project_tools == 1`. Every other turn omits `--yolo` (single_query_mode `deny` blocks dangerous commands [V3]). It also needs a toolset list without terminal or file tools, because ordinary shell commands run without a prompt [V3]. **Never use `-z`**: it hard-sets YOLO [V4].
- **Parse:** take the `result.text` of the last `type:"result"` record and fail when `exit_code != 0` [V2].
- **Home:** `~/.hermes/config.yaml` [V3]. Ready means the binary plus a nonempty config (I).
- **Gaps (U):** exact toolset names for a "chat only" turn, and whether `hermes chat` needs a running local gateway (the docs mention gateway-resolved sessions [V2]).
- **Risk:** the terminal backend `local` runs on the host [V3].

### OpenClaw (`openclaw`), ruling Q4: `--isolated`

- **argv (I):** `openclaw agent exec --isolated --json --cwd <cwd> --timeout <s> [--config-override <deny-json>] <combined>`.
- **Auth:** `--isolated` ignores the ambient config [V7], so the model and provider key must reach the run explicitly. Candidates: `--model` plus an env-var key from Hush pass (I). Whether `--isolated` allows env provider keys is U, to be confirmed in M2 with a stub.
- **Gate:** if `project_tools == 0`, pass `--config-override '{"tools":{"deny":["exec", ...]}}'` [V7]. The full deny list (write, process, browser) is U.
- **Parse:** `final` when `ok:true`. Exit 2 means timeout [V7].
- **Risks:**
  - `AGENTS.md` in the cwd is still injected [V7].
  - Keep the default temp state; a reused `--state-dir` takes an exclusive lock [V7].

### NVIDIA NemoClaw (`nemoclaw`), ruling Q2

- **Identity and maturity [V12, V17]:**
  - Official repo `NVIDIA/NemoClaw`, Apache-2.0.
  - The README calls it an "alpha project" with best-effort maintainer review.
  - No GitHub releases; tags run v0.0.x (latest v0.0.135).
- **Prerequisites [V18]:**
  - Node 22.19+ and npm 10+.
  - Docker Engine/Desktop/Colima (default) or rootless Podman.
  - An OpenShell gateway: `nemoclaw onboard` creates it and the sandbox.
  - 4 vCPU, 8 GB RAM and 20 GB disk minimum.
  - Membership of the `docker` group, which grants root-level daemon control.
- **Inner agents [V15]:** `nemoclaw agents list` shows `openclaw` (default), `hermes` and `langchain-deepagents-code`.
- **Headless turn, OpenClaw sandbox [V15]:**
  - Command: `nemoclaw <sandbox> agent --agent main --json --timeout <s> -m <combined>`.
  - The wrapper forwards verbatim to `openclaw agent` inside the sandbox via `openshell sandbox exec`.
  - It needs a target selector and exits 1 on `incomplete_turn`, abandoned or timeout.
  - The JSON envelope is `{payloads, meta}` or `{status, result:{payloads, meta}}`.
- **Headless turn, Hermes sandbox [V16]:**
  - The `agent` wrapper refuses.
  - Instead, POST `/v1/chat/completions` to the sandbox's OpenAI-compatible API on `127.0.0.1:<8642-8652>`, with Bearer `nemohermes <name> gateway-token --quiet`.
- **Reuse:**
  - The OpenClaw path reuses the M2 argv builder and `final`/`payloads` parser, behind a `nemoclaw <sandbox> agent` prefix.
  - The Hermes path is an API-family call (`hush_inference_stream`, `hush_inference.c:556`) with a per-sandbox host and token, but the agent behind it still runs tools.
- **Isolation [V15]:**
  - Tools and shell run inside the OpenShell sandbox, not on the host.
  - Egress is set by the policy tier (`restricted`/`balanced` default/`open`/`personal`).
  - `--host-mount` is read-only only.
  - Credentials stay in OpenShell provider storage.
- **Gate (#322):** the wrapper has no per-turn auto-approve or tool flag (U), and the inner agent can run shell in the sandbox.
  - If `project_tools == 1`: run the turn.
  - Otherwise, OpenClaw sandbox: forward the same deny override as M2 (`--config-override` support through `openclaw agent` is U). If that cannot be proven with a stub, **refuse**.
  - Otherwise, Hermes sandbox: **refuse**, since the API takes no per-turn tool restriction (U).
  - Refusal reason: `HUSH_JOB_ERR:NemoClaw runs tools in its sandbox; a person must ask for this turn (owner's note or the owner's Yes in a project thread)`.
- **Gaps (U):**
  - Mapping the Hush cwd to the sandbox: mounts are read-only, and `upload`/`download` exist.
  - Whether `openclaw agent` (Gateway-backed) honours `--config-override`.
  - Hermes API tool limits.
  - How many concurrent turns one sandbox allows.
  - A readiness probe: `nemoclaw <name> status --json` (I).

### NanoClaw (`nanoclaw`), ruling Q3: GO behind the person gate

- **Integration (I):** speak the `data/cli.sock` JSON-line protocol (`{"text":...}`) directly from C, instead of exec'ing `pnpm run chat` from the NanoClaw checkout [V14].
- **Gate:** every NanoClaw turn is shell-capable because bash is always available in its container [V13]. So `project_tools == 1` is required.
  - Otherwise the adapter refuses before connecting, with: `HUSH_JOB_ERR:NanoClaw always has a shell in its container; a person must ask for this turn (owner's note or the owner's Yes in a project thread)`.
  - The refusal goes out through `hush_agent_worker_err` (`agent_process.c:228-240`), so the thread sees a clear diagnostic.
- **Docker isolation [V13, V19]:**
  - One Docker container per session. Each spawn uses `--cap-drop=ALL` and `no-new-privileges`, and the agent runs non-root.
  - Only explicit mounts (session folder, group workspace, read-only shared code; its own config is re-mounted read-only). No host home, `.ssh` or Docker socket. Extra mounts are checked against `~/.config/nanoclaw/mount-allowlist`.
  - Credentials stay outside the container through the credential gateway.
  - Egress lockdown is **off by default**. When on, the container joins a Docker `--internal` network where the vault gateway is the only hop, and NanoClaw refuses to spawn if lockdown cannot be established. Hush docs should recommend turning it on.
  - The daemon and Docker run outside Hush's lifecycle. Hush must document that the operator owns `nanoclaw` service health, and must not start or stop it.
- **Risks:**
  - One client at a time [V14]. Hush must serialise NanoClaw turns: at most one in flight, and others are refused with a "busy" reason (I).
  - The 2 s-silence exit can truncate replies [V14].
  - The Hush project cwd is not visible in the container unless the operator allowlists a mount (U).

### Copilot / Goose: tracked in #333 (P1, Floor, before M1)

Copilot's `--allow-all-tools` (`agent_process.c:40,457`) and Goose's `run` with no approval flag (`:556-559`) ignore `project_tools`. Goose `GOOSE_MODE` defaults to `auto` (V: https://github.com/block/goose/blob/main/documentation/docs/guides/config-files.md L52). The fix and test plan are in #333.

## 4. Gate rule for every new adapter (#322/#323)

> Any flag, env var or config that auto-approves tools or grants shell/file write is emitted **only** when `job->project_tools == 1`. That value is set solely by `hush_agent_apply_project_cwd` behind `person_gate`. The default argv must be the least-privileged mode the tool offers. If a tool has no least-privileged mode (NanoClaw; NemoClaw unless the deny override is proven), the adapter refuses to start without `project_tools`, with a `HUSH_JOB_ERR:` reason naming the gate.

## 5. Milestones (one small DRAFT PR each, in CoS order)

| Order | PR | Scope | Open (U) |
|---|---|---|---|
| 0 | **#333 fix** (Floor) | Gate Copilot `--allow-all-tools` and Goose `GOOSE_MODE` on `project_tools` | Copilot headless without the flag |
| 1 | M1 `gb/provider-hermes` | Id `hermes`, meta row, home detect (`~/.hermes/config.yaml`), pure `hush_agent_hermes_argv` (Cline pattern), stream-json parse, `--yolo` only when gated, never `-z` | Toolset names; gateway need |
| 2 | M2 `gb/provider-openclaw` | Id `openclaw`, `agent exec --isolated --json`, deny override when not gated, `final` parse | Deny list; key passing under `--isolated` |
| 3 | M4 `gb/provider-nanoclaw` | Socket client, refuse unless `project_tools`, serialise to 1 in flight, Docker-isolation note in UI_SPEC | Mounts for the project dir |
| 4 | M3 `gb/provider-nemoclaw` | Id `nemoclaw`. OpenClaw sandbox via `nemoclaw <sb> agent` reusing M2. Hermes sandbox via the API path. Refuse unless gated, or deny proven | Override passthrough; cwd mapping; concurrency |

**Each PR must include:**

- Stub tests on the `tests/test_cline_gate.c` model, named `tests/test_<id>_gate.c`. A stub binary or socket goes first on PATH under a scratch HOME/HUSH_HOME.
- Argv/refusal assertions for owner, guest, guest-in-owner-thread and guest+Yes, plus reverse mutants, plus a parse unit test on canned stdout.
- The new binary name added to the sentinel list (`hush-c/Makefile:193`, `hush-c/tests/check_hermetic_path.sh:14`). This keeps the canary at 0: no real tool is ever spawned.
- `HUSH_PROVIDER_COUNT` and the UI_SPEC wire-id list updated.
- CI green on the exact head (`make test`).
- Draft only, `Refs #322`.

## 6. Questions for CoS: all resolved (section 0)

- Q1, Q3, Q4 and Q5 were resolved at 09:45 ET. Q2 was resolved at 09:48 ET (NemoClaw).
- The remaining unknowns are implementation-level and are listed per milestone in section 5.
