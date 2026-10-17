# PROVIDERS+ phase 1: research and plan (Hermes, OpenClaw, NVIDIA NeMo, NanoClaw)

Status: plan only. This PR adds no product code. Base: `main` @ `13511bd48b47` (#323).
Refs #322, #311 (person gate). Researched 2026-10-17 (America/Toronto).

Tags: **V** = VERIFIED_EXTERNAL (URL given), **I** = INFERRED, **U** = UNKNOWN.

## 1. Findings

| Provider | Identity (official) | Ambiguity | Headless method | Auth | Output | Approval / shell | License | Local / hosted | Fit |
|---|---|---|---|---|---|---|---|---|---|
| Hermes | **Hermes Agent** CLI, `NousResearch/hermes-agent` [V1] | Yes: Hermes Agent CLI vs Hermes 4 model family (weights on HF, served via OpenAI-compatible Nous Portal API) [V5] vs NemoClaw's Hermes sandbox [V12] | `hermes chat -q "<p>" -Q` answers and exits; `--oneshot` implied on non-TTY [V2] | `hermes setup` / `hermes model`; Nous Portal OAuth or provider keys; `--provider custom --base-url --api-key` [V2] | `--format stream-json` JSONL, terminal `result` record carries `text`, `exit_code` [V2] | `--yolo` skips approvals [V2]; `approvals.single_query_mode` default `deny` for `-q` [V3]; **`-z` forces `HERMES_YOLO_MODE=1`** [V4]; `-t/--toolsets <csv>` [V2]; terminal tool runs non-dangerous commands without a prompt [V3] | MIT [V1]; Hermes 4 weights `llama3` [V5] | Local CLI (Python), any hosted or local model | **Good.** Closest to Cline: one argv, JSON stream, explicit approval flag. |
| OpenClaw | `openclaw/openclaw`, formerly Warelay, CLAWDIS, Clawdbot, Moltbot (renamed 2026-01-30) [V6] | Low. One project. NemoClaw (NVIDIA) wraps it [V12] | `openclaw agent exec "<p>" --cwd <dir> --timeout <s> --json` runs one embedded turn with no Gateway [V7] | Ambient config and stored creds by default; `--isolated`, `--config <file>`, `--auth-env-only` [V7] | `--json` stable envelope `{ok,status,final,...}`; exit 0/1/2(timeout) [V7] | `agent exec` defaults: sandbox off, `coding` profile, **full exec policy** [V7]; `--config-override '{"tools":{"deny":["exec"]}}'` [V7]; `tools.exec.mode deny/allowlist/ask/auto/full` [V8] | MIT [V6] | Local Node CLI plus optional Gateway; any model | **Good.** Shell on by default, so the deny override is required unless a person gated the turn. |
| NVIDIA NeMo | **Unresolved.** Candidates: (a) NIM LLM API (OpenAI-compatible `integrate.api.nvidia.com/v1/chat/completions`) [V9]; (b) NeMo Agent Toolkit `nat` CLI [V10]; (c) NeMo framework (`NVIDIA/NeMo` now redirects to `NVIDIA-NeMo/Speech`, a training framework) [V11]; (d) NeMo Guardrails (rails layer, not an agent) [V11]; (e) **NemoClaw** (OpenClaw/Hermes inside OpenShell) [V12] | **High.** CoS must pick | (a) HTTP API, no CLI; (b) `nat run --config_file <yml> --input "<p>"` [V10]; (e) `nemoclaw` CLI, alpha [V12] | (a) Bearer `NVIDIA_API_KEY` [V9]; (b) per workflow YAML (U) | (a) OpenAI chat JSON/SSE [V9]; (b) console `Workflow Result:` text [V10]: no JSON flag found (U) | (a) none: text only; (b) tool use is set by workflow config, no approval flag found (U); first `nat` run shows a telemetry consent prompt that defaults to yes [V10] | (a) NVIDIA service terms (U); (b) Apache-2.0 [V10]; (e) Apache-2.0 [V12] | (a) hosted or self-hosted NIM; (b) local Python; (e) local sandbox | (a) **Excellent**: an API-family entry, no shell. (b) Weak: needs a workflow YAML per robot. (e) Defer: duplicates the OpenClaw/Hermes adapters plus OpenShell. |
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
[V12] https://github.com/NVIDIA/NemoClaw · [V13] https://github.com/nanocoai/nanoclaw · [V14] https://docs.nanoclaw.dev/channels/cli

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

### OpenClaw (`openclaw`)

- **argv (I):** `openclaw agent exec --json --cwd <cwd> --timeout <s> [--config-override <deny-json>] <combined>`.
- **Gate:** if `project_tools == 0`, pass `--config-override '{"tools":{"deny":["exec", ...]}}'` [V7]. The full deny list (write, process, browser) needs verification (U).
- **Parse:** `final` when `ok:true`. Exit 2 means timeout [V7].
- **Home:** `~/.openclaw` [V6].
- **Risks:**
  - `agent exec` inherits the operator's config and credentials unless `--isolated` or `--config` is passed [V7].
  - Existing `AGENTS.md` in cwd is injected [V7].
  - `agent exec` takes an exclusive state lock if `--state-dir` is reused, so keep the default temp state [V7].

### NVIDIA NeMo

- **If (a) NIM:** add an `nvidia-api` API-family row, host `https://integrate.api.nvidia.com/v1`, key in pass. The existing streaming path and `/chat/completions` cover it (`hush_inference.c:165-186`). There is no shell, so no gate change. It is possible today with zero code via the `custom` provider (I).
- **If (b) `nat`:** each robot needs a workflow YAML. There is no JSON output, the approval surface is U, and there is a first-run telemetry prompt [V10]. Not recommended.
- **If (e) NemoClaw:** it rides on the Hermes and OpenClaw adapters. Phase 2+.

### NanoClaw

- **Integration:** speak the `data/cli.sock` JSON-line protocol directly (I), or exec `pnpm run chat` from the NanoClaw checkout dir.
- **Gate:** there is no per-turn tool switch (U) and bash is always available in the container, so **every NanoClaw turn is shell-capable**. It must require `person_gate` (proposed: refuse to start unless `project_tools == 1`).
- **Risks:**
  - A single client supersedes the last [V14], which conflicts with `HUSH_AGENT_JOBS_MAX` parallelism.
  - The 2 s-silence exit can truncate replies [V14].
  - The daemon and Docker are outside Hush lifecycle control.

### Pre-existing (not in scope, for CoS)

- Copilot's `--allow-all-tools` (`agent_process.c:40,457`) and Goose `run` (`:556-559`) are not tied to `project_tools`.
- Goose's default tool-approval mode is U. Consider a follow-up issue to route both through the person gate the way Cline is.

## 4. Gate rule for every new adapter (#322/#323)

> Any flag, env var or config that auto-approves tools or grants shell/file write is emitted **only** when `job->project_tools == 1`. That value is set solely by `hush_agent_apply_project_cwd` behind `person_gate`. The default argv must be the least-privileged mode the tool offers. If a tool has no least-privileged mode (NanoClaw), the adapter refuses to start without `project_tools`.

## 5. Milestones (one provider per small DRAFT PR, ordered by readiness)

| # | PR | Scope | Blocked on |
|---|---|---|---|
| M1 | `gb/provider-hermes` | Id `hermes`, meta row, home detect, pure `hush_agent_hermes_argv` (Cline pattern), stream-json parse, UI_SPEC rows | toolset names (U) |
| M2 | `gb/provider-openclaw` | Id `openclaw`, `agent exec --json`, deny override when not gated, `final` parse | deny list (U) |
| M3 | `gb/provider-nvidia` | `nvidia-api` API-family row (NIM), or whichever candidate CoS picks | CoS choice |
| M4 | `gb/provider-nanoclaw` | Socket client, gate-every-turn | CoS go/no-go |

**Each PR must include:**

- Stub tests on the `tests/test_cline_gate.c` model, named `tests/test_<id>_gate.c`. A stub binary goes first on PATH under a scratch HOME/HUSH_HOME. The tests assert argv for owner, guest, guest-in-owner-thread and guest+Yes. They also cover a parse unit test on canned stdout.
- The new binary name added to the sentinel list (`hush-c/Makefile:193`, `hush-c/tests/check_hermetic_path.sh:14`). This keeps the canary at 0: no real tool is ever spawned.
- `HUSH_PROVIDER_COUNT` and the UI_SPEC wire-id list updated.
- CI green on the exact head (`make test`).
- Draft only, `Refs #322`.

## 6. Open questions for CoS

1. **Hermes:** Hermes Agent CLI (recommended) or the Hermes 4 model via an OpenAI-compatible endpoint (already possible with `custom`)? Or both?
2. **NVIDIA NeMo:** which one? NIM API (recommended, lowest risk), NeMo Agent Toolkit `nat`, NemoClaw, Guardrails, or the training framework (no fit)?
3. **NanoClaw:** proceed given the daemon, Docker and single-client limits, and the gate-every-turn rule? Or defer?
4. **OpenClaw:** run with `--isolated` (no ambient operator creds) or with the operator's config?
5. Should the pre-existing Copilot and Goose ungated tool access get its own issue?
