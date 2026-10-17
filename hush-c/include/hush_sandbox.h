/* hush_sandbox.h: the jail an agent child runs in (#336 parts 1 and 2).
 *
 * An agent CLI runs with the user's uid, so without a jail it could read
 * session.token and keys.vault, connect to the relay port, or read the
 * relay's environment. Just before exec the child:
 *   - gets a fresh per-job HOME (mode 0700) with only allowlisted provider
 *     auth paths linked or bound read-only; never ~/.password-store,
 *     ~/.gnupg, ~/.ssh, or $HUSH_HOME; an unknown auth path refuses the turn
 *     and names the path;
 *   - keeps only allowlisted environment variables (PATH, HOME, LANG, TERM,
 *     locale, proxy and CA settings, and the variables a provider CLI needs);
 *     nothing named HUSH_*, so no HUSH_KEY_PASS, HUSH_HOME or HUSH_OP_*;
 *   - closes every inherited descriptor above stderr;
 *   - prefers Landlock ABI ≥ 4 (deny HUSH_HOME/config/legacy and the real
 *     HOME tree, grant the stub HOME and auth paths, deny the relay port,
 *     scope ptrace); otherwise falls back to bwrap --unshare-pid --proc /proc
 *     with no_new_privs when that isolates the relay's /proc and ptrace;
 *     with neither, the turn is refused and the reason is named.
 * Tests may set HUSH_SANDBOX_BACKEND=landlock|bwrap|none. */

#ifndef HUSH_SANDBOX_H
#define HUSH_SANDBOX_H

#include <stdint.h>

#include "hush_status.h"

enum {
    HUSH_SANDBOX_DENY_MAX = 8,
    HUSH_SANDBOX_AUTH_MAX = 8,
    HUSH_SANDBOX_PATH_MAX = 4096,
    HUSH_SANDBOX_ENV_MAX = 256,
    HUSH_SANDBOX_ABI_NET = 4,
    HUSH_SANDBOX_WHY_MAX = 256
};

typedef enum {
    HUSH_SANDBOX_BACKEND_NONE = 0,
    HUSH_SANDBOX_BACKEND_LANDLOCK = 1,
    HUSH_SANDBOX_BACKEND_BWRAP = 2
} hush_sandbox_backend_t;

/* What one child may not touch. deny[] and cwd are canonical paths. */
typedef struct {
    char deny[HUSH_SANDBOX_DENY_MAX][HUSH_SANDBOX_PATH_MAX];
    int ndeny;
    char auth_ro[HUSH_SANDBOX_AUTH_MAX][HUSH_SANDBOX_PATH_MAX];
    int nauth;
    char cwd[HUSH_SANDBOX_PATH_MAX];
    char stub_home[HUSH_SANDBOX_PATH_MAX];
    char real_home[HUSH_SANDBOX_PATH_MAX];
    uint16_t relay_port;
} hush_sandbox_policy_t;

/* Fills policy: deny trees, stub HOME, allowlisted auth binds, cwd, port. */
hush_status_t hush_sandbox_policy(hush_sandbox_policy_t *policy, const char *cwd,
                                  uint16_t relay_port);

/* 1 when name=value may reach an agent child. */
int hush_sandbox_env_allowed(const char *entry);

/* Replaces environ with its allowlisted entries (pointers, no copies). */
void hush_sandbox_scrub_env(void);

/* Landlock ABI of this kernel, or 0 when Landlock is unavailable. */
int hush_sandbox_abi(void);

/* Which jail runs: Landlock when ABI ≥ 4, else bwrap when present, else none.
 * HUSH_SANDBOX_BACKEND overrides detection for tests. */
hush_sandbox_backend_t hush_sandbox_backend(void);

/* Last refusal reason (static buffer), or "" when none. */
const char *hush_sandbox_why(void);

/* 1 when path may be bound read-only as provider auth (allowlisted and not
 * under a forbidden tree). */
int hush_sandbox_auth_allowed(const char *path);

/* Closes inherited descriptors, scrubs the environment, prepares stub HOME,
 * and enters Landlock when that is the backend. Call in the child. */
hush_status_t hush_sandbox_enter(const hush_sandbox_policy_t *policy);

/* Jails this child and execs file. Returns only on failure. */
hush_status_t hush_sandbox_exec(const char *cwd, uint16_t relay_port, const char *file,
                                char **argv);

#endif /* HUSH_SANDBOX_H */
