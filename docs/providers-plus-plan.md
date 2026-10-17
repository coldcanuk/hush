# PROVIDERS+ phase 1: research and plan (Hermes, OpenClaw, NVIDIA NemoClaw, NanoClaw)

Status: plan only. This PR adds no product code. Base: `main` @ `13511bd48b47` (#323).
Refs #322, #311 (person gate), #333 (Copilot/Goose gate fix), #336 (loopback token Set-Cookie). Researched 2026-10-17 (America/Toronto). CoS rulings added 09:45, 09:48 and 10:12 ET. Gauge review of 158c050b addressed in section 7.

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

## 1. Findings

| Provider | Identity (official) | Ambiguity | Headless method | Auth | Output | Approval / shell | License | Local / hosted | Fit |
|---|---|---|---|---|---|---|---|---|---|
| Hermes | **Hermes Agent** CLI, `NousResearch/hermes-agent` [V1] | Yes: Hermes Agent CLI vs Hermes 4 model family (weights on HF, served via OpenAI-compatible Nous Portal API) [V5] vs NemoClaw's Hermes sandbox [V12] | `hermes chat -q "<p>" -Q` answers and exits; `--oneshot` implied on non-TTY [V2] | `hermes setup` / `hermes model`; Nous Portal OAuth or provider keys; `--provider custom --base-url --api-key` [V2] | `--format stream-json` JSONL, terminal `result` record carries `text`, `exit_code` [V2] | `--yolo` skips approvals [V2]; `approvals.single_query_mode` default `deny` for `-q` [V3]; **`-z` forces `HERMES_YOLO_MODE=1`** [V4]; `-t/--toolsets <csv>`, `--ignore-user-config` [V2]; toolset `safe` = no file/terminal/code [V21]; terminal tool runs non-dangerous commands without a prompt [V3] | MIT [V1]; Hermes 4 weights `llama3` [V5] | Local CLI (Python), any hosted or local model | **Good.** Closest to Cline: one argv, JSON stream, explicit approval flag. |
| OpenClaw | `openclaw/openclaw`, formerly Warelay, CLAWDIS, Clawdbot, Moltbot (renamed 2026-01-30) [V6] | Low. One project. NemoClaw (NVIDIA) wraps it [V12] | `openclaw agent exec "<p>" --cwd <dir> --timeout <s> --json` runs one embedded turn with no Gateway [V7] | Ambient config and stored creds by default; `--isolated` drops ambient config only; `--auth-env-only` cannot be combined with `--config`/`--config-override` [V7] | `--json` stable envelope `{ok,status,final,...}`; exit 0/1/2(timeout) [V7] | `agent exec` defaults: sandbox off, `coding` profile, **full exec policy** [V7]; `--config-override '{"tools":{"deny":["exec"]}}'` [V7]; `tools.exec.mode deny/allowlist/ask/auto/full` [V8] | MIT [V6] | Local Node CLI plus optional Gateway; any model | **Good.** Shell on by default, so the deny override is required unless a person gated the turn. |
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
[V20] https://docs.openclaw.ai/tools (L87-88 tool names) · [V21] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/reference/toolsets-reference.md (`safe`) · [V21b] https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/tools.md (terminal backends)

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
9. **Clock.** The job budget is 300 s with project tools, else 80 s for Cline (`hush-c/include/hush_agent.h:17-19`). `SIGTERM` is followed by `SIGKILL` after 3 s (`hush_agent_internal.h:30`).
10. **Hermetic tests.** `make test` puts refusing sentinels (exit 97) ahead of `goose grok codex copilot cline ollama` (`hush-c/Makefile:178-195`), and `tests/check_hermetic_path.sh:14-24` asserts it. `tests/test_cline_gate.c` drives the real mention path. A stub `cline` first on PATH records its argv (`:85-100`), under a scratch HOME/HUSH_HOME/CLINE_DATA_DIR (`:103-125`). It covers 4 cases: owner auto, guest auto, guest in an owner thread, guest plus owner Yes (`:251-305`). CI runs `./configure && make && make test` (`.github/workflows/ci.yml:19-26`).

## 3. Per-provider mapping, gaps, risks

### Hermes Agent (`hermes`), rulings Q1 and 10:12

- **Ungated argv (`project_tools == 0`):** `hermes chat -q <combined> --format stream-json --ignore-user-config --ignore-rules -t safe --max-turns 2`, with `chdir(job->cwd)`. Every flag is in [V2]:
  - `--ignore-user-config` ignores `~/.hermes/config.yaml` (`.env` credentials still load), so an operator's `approvals.mode: off` or a custom toolset cannot widen the turn.
  - `-t safe` is "read-only research + media generation. No file writes, no terminal, no code execution" [V21].
  - `--ignore-rules` skips `AGENTS.md`/memory injection.
- **Child env:** the worker calls `unsetenv("HERMES_YOLO_MODE")` before `execvp`. Today the child inherits the relay environ unscrubbed (`agent_process.c:65-74`), and `HERMES_YOLO_MODE` turns YOLO on [V3, V4].
- **Never** pass `-z` (it hard-sets YOLO [V4]) or `--yolo` on an ungated turn.
- **Gated argv (`project_tools == 1`, only after #336 is merged):** `hermes chat -q <combined> --format stream-json --ignore-rules --max-turns 8 --yolo`. This keeps the operator config, so the operator can set `terminal.backend: docker` [V21b], which is recommended. `HERMES_YOLO_MODE` is still unset.
- **Fail closed:** the flag names above are VERIFIED. M1 must ship a stub test (`tests/test_hermes_gate.c`) that proves, for every ungated case:
  - argv contains `--ignore-user-config` and `-t safe`, and lacks `--yolo`, `-z` and any `terminal`/`file` toolset;
  - the child env lacks `HERMES_YOLO_MODE` (planted in the parent first).
  - Reverse mutants must fail the test.
  - If any of this cannot be proven, ungated turns are **refused**: `HUSH_JOB_ERR:Hermes tools need a person's ask for this turn`.
- **Provider/model:** with `--ignore-user-config`, built-in defaults apply, so Hush must pass `--provider`/`--model` [V2]. Where Hush gets those values is U (M1).
- **Parse:** take `result.text` of the last `type:"result"` record; fail if `exit_code != 0` [V2].
- **Home and readiness:** the binary plus `~/.hermes/` (I).
- **Gaps (U):**
  - Whether `-t` replaces or adds to the default toolsets. The docs say "Enable a comma-separated set" [V2], so verify once against the real binary and record it in the M1 PR.
  - Whether `hermes chat` needs a running gateway.
  - Whether `--safe-mode` (also disables plugins, hooks and MCP [V2]) suits integration better.
- **Risks:**
  - `safe` keeps web tools, so there is internet egress. Hermes web tools block loopback (SSRF guard, per Gauge OBSERVED [V3]).
  - A gated turn runs a same-uid host shell under the `local` backend [V3]. That is why M1 waits for #336 (section 4).

### OpenClaw (`openclaw`), rulings Q4 and 10:12

- **argv:** `openclaw agent exec --isolated --json --cwd <cwd> --timeout <T> [--config-override <deny>] <combined>`.
- **Auth (resolved, V):** `--auth-env-only` "loads no config at all, and pairing it with `--config` or `--config-override` is rejected" [V7]. Hush therefore does **not** use `--auth-env-only`.
  - The run uses the default `--no-auth-env-only`: stored and external CLI credentials [V7].
  - `--isolated` only means "ignore the ambient config and use only exec defaults" [V7]. It is **not** credential isolation, and this plan makes no credential-isolation claim.
  - Overrides apply after the `--isolated` base [V7].
- **Ungated (`project_tools == 0`):** `--config-override '{"tools":{"deny":["exec","process","terminal","code_execution","write","edit","apply_patch","browser"]}}'`. These are the Runtime, Files and Browser tool names [V20], and exec defaults are otherwise "sandbox off ... full execution policy" [V7].
  - M2 must prove with a stub that the override is present and unchanged on every ungated case (reverse mutants).
  - M2 must also record one real-binary check that a denied `exec` is not offered.
  - Until both exist, ungated turns are **refused**: `HUSH_JOB_ERR:OpenClaw runs shell by default; a person must ask for this turn`.
- **Gated (only after #336 is merged):** no override.
- **Parse:** `final` when `ok:true`; exit 2 means timeout [V7].
- **Risks:**
  - `AGENTS.md` in the cwd is still injected [V7].
  - Keep the default temp state; `--state-dir` takes an exclusive lock [V7].
  - Upstream default timeout is 600 s [V7], so Hush always passes `--timeout`.

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
  - The OpenClaw path reuses the M2 deny list and parser.
  - The Hermes path would be an API-family call (`hush_inference_stream`, `hush_inference.c:556`), but the agent behind it still runs tools.
- **Isolation [V15]:**
  - Tools run in the OpenShell sandbox.
  - Egress follows the policy tier (`balanced` default). Even `personal` keeps "loopback, and link-local ranges ... blocked".
  - `--host-mount` is read-only.
  - Credentials are kept by the OpenShell gateway, per the vendor claim (not tested).
- **Gate (#322):**
  - If `project_tools == 1`: run.
  - Otherwise, OpenClaw sandbox: forward the M2 deny override only if a stub proves it and a real check shows it passes through `openclaw agent`; else **refuse**.
  - Otherwise, Hermes sandbox: **always refuse**.
  - Refusal reason: `HUSH_JOB_ERR:NemoClaw runs tools in its sandbox; a person must ask for this turn`.
- **Version pin and update policy (10:12):**
  - Hush carries a compile-time allowlist of tested NemoClaw versions, starting with one pinned tag. Operators install with `NEMOCLAW_INSTALL_TAG=<pinned tag>` [V15].
  - Hush never runs `nemoclaw update`. The upstream maintained installer follows the admin-promoted `lkg` tag, which can differ from the newest tag [V15].
  - An operator may run `nemoclaw update --check` [V15]. A pin bump is a Hush PR that re-runs the M3 stub fixtures against the new envelope.
- **Fail-closed readiness (10:12):** NemoClaw is ready only if all of these hold:
  - `nemoclaw --version` [V15] is on the allowlist;
  - `nemoclaw <sb> status --json` exits 0, with `agent` equal to the expected inner agent [V15];
  - the kill switch is off.
  - Any other result, a timeout, or unparsable output means not ready, and the turn is refused with `HUSH_JOB_ERR:NemoClaw not ready (<reason>)`.
  - An unknown reply envelope fails the turn and does not fall back to raw text.
- **Kill switch (10:12, I):** an operator setting such as `providers.nemoclaw.disabled` (name chosen in M3). It marks NemoClaw not ready immediately and is checked before every spawn.
- **Gaps (U):**
  - cwd mapping (read-only mounts; `upload`/`download`).
  - `--config-override` passthrough via `openclaw agent`.
  - Hermes API tool limits.
  - Concurrency per sandbox.
  - Whether Hush's SIGKILL 3 s after SIGTERM orphans the in-sandbox turn.

### NanoClaw (`nanoclaw`), ruling Q3: GO behind the person gate

- **Integration (I):** speak the `data/cli.sock` JSON-line protocol (`{"text":...}`) directly from C [V14].
- **Gate:** bash is always available in its container [V13]. So `project_tools == 1` is required.
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

The Hush clock is 300 s with project tools, else 80 s, followed by SIGTERM and then SIGKILL 3 s later (`hush_agent.h:17-19`, `hush_agent_internal.h:30`).

| Provider | Gated | Ungated | Mechanism |
|---|---|---|---|
| Hermes | 270 s | 60 s | No CLI timeout flag found in [V2] (U); the Hush clock SIGTERMs it, plus `--max-turns 8/2` |
| OpenClaw | `--timeout 270` | `--timeout 60` | [V7]; upstream default 600 s overridden |
| NemoClaw | `--timeout 240` | refused | The wrapper bounds the wait at `T+30` s [V15], so 270 s is still under 300 s |
| NanoClaw | 270 s socket read deadline in Hush | refused | Upstream `pnpm run chat` gives up at 120 s [V14]. The daemon-side limit for direct socket use is U |

### Containers (10:12): rootless Podman preferred

- `docker` group membership is root-equivalent [V18]. With #336 open, any same-uid host-shell turn (Cline, Copilot, Goose, gated Hermes/OpenClaw) would become root through the Docker socket (I).
- **Rule:** the Hush relay uid must not be in the `docker` group. Prefer rootless Podman:
  - NemoClaw supports it (`NEMOCLAW_GATEWAY_RUNTIME=podman` [V18]).
  - NanoClaw and Hermes `docker` backend support for Podman is U.
  - If only Docker works, the provider runs under a separate operator uid, or it is not enabled.
- **Required in docs:**
  - Never mount `$HOME` or `$HUSH_HOME` (NemoClaw `--host-mount`, NanoClaw mount allowlist).
  - Never use host networking.
  - Enable NanoClaw egress lockdown.

## 4. Gate rule for every new adapter (#322/#323, #333, #336)

> Any flag, env var or config that auto-approves tools or grants shell/file write is emitted **only** when `job->project_tools == 1`. That value is set solely by `hush_agent_apply_project_cwd` behind `person_gate`. The default argv must be the least-privileged mode the tool offers, **proven by a stub test with reverse mutants**. If a tool has no proven least-privileged mode (NanoClaw; NemoClaw unless the deny is proven; Hermes/OpenClaw until their stub proofs land), the adapter refuses without `project_tools`, with a `HUSH_JOB_ERR:` reason naming the gate.
>
> **`person_gate` is not enough for a host shell while #336 is open.** A gated same-uid host shell can read the owner token (`$HUSH_HOME/session.token`, or the loopback Set-Cookie at `hush_http.c:286-290`) and approve its own turns (#336). So:
> - no adapter that grants a host shell (gated Hermes, gated OpenClaw) merges before #336 is fixed;
> - container providers (NanoClaw, NemoClaw) must keep `$HOME`/`$HUSH_HOME` unmounted and avoid host networking.

## 5. Milestones (one small DRAFT PR each, CoS order 10:12)

| Order | PR | Scope | Open (U) |
|---|---|---|---|
| 0 | **#336 fix** | Loopback token Set-Cookie (P1). Blocks every host-shell grant | Per #336 |
| 1 | **#333 fix** (Floor) | Gate Copilot `--allow-all-tools` and Goose `GOOSE_MODE` on `project_tools` | Copilot headless without the flag |
| 2 | M1 `gb/provider-hermes` | Id `hermes`. Ungated: `--ignore-user-config --ignore-rules -t safe`, `HERMES_YOLO_MODE` unset, refuse unless the stub proof passes. Gated: `--yolo`. Timeouts 270/60 | `-t` semantics; provider/model source; gateway need |
| 3 | M2 `gb/provider-openclaw` | Id `openclaw`. `agent exec --isolated --json --timeout 270/60`. Stored-creds auth, no `--auth-env-only`. Deny override when ungated, refuse until proven | Real-binary deny check |
| 4 | M4 `gb/provider-nanoclaw` | Socket client. Refuse unless `project_tools`. One turn in flight. 270 s deadline. Container rules | Podman; project-dir mount; daemon limit |
| 5 | M3 `gb/provider-nemoclaw` | Id `nemoclaw`. Version allowlist, fail-closed readiness, kill switch, Podman preferred. `agent --timeout 240 --session-id`. Refuse unless gated (Hermes sandbox always refused) | Override passthrough; cwd mapping; concurrency |

**Each PR must include:**

- Stub tests on the `tests/test_cline_gate.c` model, named `tests/test_<id>_gate.c`. A stub binary or socket goes first on PATH under a scratch HOME/HUSH_HOME.
- Argv/env/refusal assertions for owner, guest, guest-in-owner-thread and guest+Yes, plus reverse mutants and a parse unit test.
- The new binary added to the sentinel list (`hush-c/Makefile:193`, `hush-c/tests/check_hermetic_path.sh:14`), so the canary stays at 0.
- `HUSH_PROVIDER_COUNT` and the UI_SPEC wire ids updated.
- CI green on the exact head.
- Draft only, `Refs #322`.

## 6. Questions for CoS: all resolved (section 0)

Q1-Q5 were resolved at 09:45/09:48 ET, and the 10:12 rulings are applied. Implementation unknowns are listed per milestone.

## 7. Gauge review of 158c050b (FAIL, 10:10 ET): disposition

| Finding | Where addressed |
|---|---|
| P1-1 C-5/#336 not gating host-shell M1/M2 | Section 0 order; section 4 #336 rule; section 5 rows 0, 2, 3; Hermes and OpenClaw "Gated" bullets |
| P1-2 Hermes ungated not fail-closed | Hermes section: mandatory `--ignore-user-config -t safe`, `HERMES_YOLO_MODE` unset, stub proof or refuse |
| P2-1 "only switch" overclaim | Section 2 item 4, reworded as the target rule with today's exceptions (#333) |
| P2-2 OpenClaw deny/auth conflict, `--isolated` wording | OpenClaw section (auth resolved, deny list [V20], refuse until proven); section 0 Q4 row |
| P2-3 no timeouts | Timeouts table |
| P2-4 NemoClaw pin/update/fallback | NemoClaw section: version pin, readiness, kill switch |
| P2-5 docker group + C-5 | Containers subsection; NemoClaw prerequisites |
| P3-2 NemoClaw `--timeout` order / orphan | NemoClaw headless and gaps |
| P3-3 mounts / host network | Containers "Required in docs"; section 4 |
| P3-4 releases | NemoClaw identity (CI prerelease noted) |
| P3-5 shared session | NemoClaw `--session-id hush-<thread>` |
| P3-6 NanoClaw socket hijack | NanoClaw risks |
| P3-7 [V16] not re-fetched | Re-fetched 2026-10-17 by the plan author: `nemohermes <name> agent` section quoted as above |
