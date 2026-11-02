/* hush_cli_gate.h: provider CLI permission policy and readiness (#356).
 * Grok never runs with --always-approve: it runs in dontAsk mode with
 * explicit allow and deny rules. Grok and Copilot turns start only after a
 * deterministic readiness probe (no model call): the CLI's --version is at
 * or above a pinned minimum and its help lists every permission flag Hush
 * relies on. Anything else refuses the turn; there is no fallback. */

#ifndef HUSH_CLI_GATE_H
#define HUSH_CLI_GATE_H

#include <stddef.h>

#include "hush_sandbox.h"
#include "hush_status.h"

enum {
    /* Bytes kept from one probe's output. */
    HUSH_CLI_PROBE_MAX = 65536,
    /* Wall clock for one probe, in milliseconds. */
    HUSH_CLI_PROBE_MS = 5000,
    /* argv slots hush_cli_grok_policy fills. */
    HUSH_CLI_GROK_POLICY_SLOTS = 16
};

/* A dotted version: major.minor.patch. */
typedef struct {
    unsigned long major;
    unsigned long minor;
    unsigned long patch;
} hush_cli_version_t;

/* What one provider CLI must show before Hush runs a prompt through it. */
typedef struct {
    const char *bin;
    const char *version_arg;
    const char *help_arg;
    hush_cli_version_t min;
    /* NULL-terminated tokens the help text must contain as whole words. */
    const char *const *flags;
    /* The HUSH_JOB_ERR reason when the probe fails. */
    const char *why;
} hush_cli_spec_t;

extern const hush_cli_spec_t hush_cli_spec_grok;
extern const hush_cli_spec_t hush_cli_spec_copilot;

/* 1 when text holds a dotted version at or above min. Pure. */
int hush_cli_version_ok(const char *text, const hush_cli_version_t *min);

/* 1 when text holds every flag in flags as a whole token. Pure. */
int hush_cli_help_ok(const char *text, const char *const *flags);

/* Runs spec's version and help probes, each jailed like a turn, and checks
 * both. HUSH_OK only when both probes exit 0 within HUSH_CLI_PROBE_MS and
 * pass. HUSH_ERR_IO when the jail refused a probe: why then holds the jail's
 * reason (first line, possibly ""). HUSH_ERR_DENIED for anything else. Call
 * only in a forked child that is about to exec the CLI: it resets SIGCHLD to
 * SIG_DFL to reap its probes. No cache: it runs once per turn. */
hush_status_t hush_cli_ready(const hush_cli_spec_t *spec, const hush_sandbox_job_t *jail,
                             char *why, size_t whysz);

/* Writes Grok's permission argv (dontAsk; Read and Grep allowed; Edit
 * allowed only when gated, denied otherwise; Bash, MCPTool, WebFetch and
 * WebSearch denied) into argv. Returns HUSH_CLI_GROK_POLICY_SLOTS, or 0
 * when cap is smaller. Pure: argv borrows string literals. */
size_t hush_cli_grok_policy(char **argv, size_t cap, int gated);

#endif /* HUSH_CLI_GATE_H */
