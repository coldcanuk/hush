# PLAN_HARNESS_ENGINE — Provider Capability Matrix + Routing

RDAP Phase 2 (architecture) → Phase 3 (implementation) plan for the Hush
harness engine. Complements [RESEARCH_HARNESS.md](../research/RESEARCH_HARNESS.md).

## Goal

Make the provider registry an *execution-capable* abstraction: every provider
declares a **capability bitmask** (tools / image / file-attach), and the harness
can query and gate those capabilities before routing work.

## Milestone 1 — Capability matrix (this plan's first commit)

Self-contained, testable, and the prerequisite for true capability routing.

### Architecture

```c
/* hush_provider.h */
#define HUSH_PROVIDER_CAP_TOOLS       (1u << 0)  /* agentic tool use          */
#define HUSH_PROVIDER_CAP_IMAGE       (1u << 1)  /* image analysis            */
#define HUSH_PROVIDER_CAP_FILE_ATTACH (1u << 2)  /* consumes file context     */

typedef unsigned int hush_provider_caps_t;

unsigned int hush_provider_capabilities(const char *id);  /* 0 on unknown id  */
int hush_provider_can(const char *id, hush_provider_caps_t cap);
```

`hush_provider_meta_t` gains `unsigned int caps;`, and
`hush_provider_status_t` gains `unsigned int caps;` (copied in
`hush_provider_status()`), so the UI sees it via `/api/provider`.

### Declared truth table (native capability, conservative)

| id            | tools | image | file |
|---------------|:-----:|:-----:|:----:|
| goose         |   ✓   |   —   |  ✓   |
| grok-build    |   ✓   |   ✓   |  ✓   |
| codex         |   ✓   |   ✓   |  ✓   |
| cline         |   ✓   |   ✓   |  ✓   |
| gemini-api    |   —   |   ✓   |  —   |
| xai-api       |   —   |   ✓   |  —   |
| openai-api    |   —   |   ✓   |  —   |
| anthropic-api |   —   |   ✓   |  —   |
| deepseek-api  |   —   |   —   |  —   |

CLI/editor agents (goose/grok-build/codex/cline) are full agents: tools + file
context. Vision where the provider natively supports it. The REST (api) family
has no harness tool/file routing today, so only native image/vision is declared.
deepseek is text-only.

Note: the harness does **not** yet *route* image/file-attach to these providers;
the matrix is the authoritative capability source + future blocking gate
(`hush_provider_can()`), which is the correct direction for subscore Z.

## Milestone 2 (follow-on) — capability gating at dispatch

Wire `hush_provider_can()` into `hush_agent.c` mention dispatch so an image
mention or file context for a text-only robot is refused with a clean error
instead of silently dropped.

## Milestone 3 (follow-on) — launch-time auto-update scanner

Add `hush_provider_update_all()`: scan for installed CLI binaries and run their
documented update routine (`grok update`, `codex update`, `goose update`, …)
silently at launch, with per-provider skip on absence and a bounded timeout.

## Milestone 4 (follow-on) — new providers

Add `agy` (Antigravity, spawn-only no-wrap), `copilot` (OAuth), `ollama`
(local), and `custom` (OpenAI-compatible base URL) with honest capability flags
and UI warnings where a ToS/allow-list constraint applies.

## Milestone 5 (follow-on) — token/context segmentation

New `hush_seg.c` module: plaintext + markdown structural chunker with
crash-proof parsing, feeding reduced payloads to text-only models.

## Verification

Every milestone: `make` (strict C11 flags) + `make test` must stay green.
Commit per milestone on `gb/harness-engine`.
