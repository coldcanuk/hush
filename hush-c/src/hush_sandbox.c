/* hush_sandbox.c: stub HOME, auth binds, Landlock or bwrap jail for agents. */

#define _GNU_SOURCE

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_sandbox.h"

#if defined(__linux__)
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

extern char **environ;

static char g_sandbox_why[HUSH_SANDBOX_WHY_MAX];

/* Exact names a provider CLI may need; everything else, HUSH_* included, is
 * dropped. */
static const char *const HUSH_SANDBOX_ENV_NAMES[] = {
    "PATH", "HOME", "LANG", "LANGUAGE", "TERM", "COLORTERM", "NO_COLOR", "TZ",
    "USER", "LOGNAME", "SHELL", "TMPDIR", "PWD", "OLDPWD", "SHLVL", "_",
    "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
    "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "ALL_PROXY",
    "http_proxy", "https_proxy", "no_proxy", "all_proxy",
    "SSL_CERT_FILE", "SSL_CERT_DIR", "NODE_EXTRA_CA_CERTS", "REQUESTS_CA_BUNDLE",
    "CURL_CA_BUNDLE",
    "XAI_API_KEY", "OPENAI_API_KEY", "OPENAI_BASE_URL", "ANTHROPIC_API_KEY",
    "GEMINI_API_KEY", "GOOGLE_API_KEY", "OPENROUTER_API_KEY",
    "GH_TOKEN", "GITHUB_TOKEN", "GH_HOST",
};

static const char *const HUSH_SANDBOX_ENV_PREFIXES[] = {
    "LC_", "GROK_", "CODEX_", "COPILOT_", "GOOSE_", "OLLAMA_", "CLINE_",
};

/* Relative to the operator HOME: the only auth trees a child may see. */
static const char *const HUSH_SANDBOX_AUTH_REL[] = {
    ".grok", ".codex", ".copilot", ".config/goose", ".goose", ".cline", ".ollama",
};

/* Never bound, even if named somehow. */
static const char *const HUSH_SANDBOX_AUTH_FORBID[] = {
    ".password-store", ".gnupg", ".ssh",
};

#define HUSH_SANDBOX_COUNT(a) (sizeof(a) / sizeof((a)[0]))

const char *hush_sandbox_why(void)
{
    return g_sandbox_why;
}

static void hush_sandbox_set_why(const char *why)
{
    if (why == NULL || why[0] == '\0') {
        g_sandbox_why[0] = '\0';
        return;
    }
    snprintf(g_sandbox_why, sizeof(g_sandbox_why), "%s", why);
}

static void hush_sandbox_why_path(const char *prefix, const char *path)
{
    snprintf(g_sandbox_why, sizeof(g_sandbox_why), "%s: %s", prefix,
             path != NULL ? path : "");
}

int hush_sandbox_env_allowed(const char *entry)
{
    const char *eq = NULL;
    size_t n = 0;

    if (entry == NULL || (eq = strchr(entry, '=')) == NULL || eq == entry)
        return 0;
    n = (size_t)(eq - entry);
    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(HUSH_SANDBOX_ENV_NAMES); ++i) {
        const char *name = HUSH_SANDBOX_ENV_NAMES[i];
        if (strlen(name) == n && strncmp(entry, name, n) == 0)
            return 1;
    }
    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(HUSH_SANDBOX_ENV_PREFIXES); ++i) {
        const char *prefix = HUSH_SANDBOX_ENV_PREFIXES[i];
        if (strlen(prefix) < n && strncmp(entry, prefix, strlen(prefix)) == 0)
            return 1;
    }
    return 0;
}

void hush_sandbox_scrub_env(void)
{
    static char *kept[HUSH_SANDBOX_ENV_MAX + 1];
    size_t n = 0;

    for (char **e = environ; e != NULL && *e != NULL; ++e) {
        if (n < (size_t)HUSH_SANDBOX_ENV_MAX && hush_sandbox_env_allowed(*e))
            kept[n++] = *e;
    }
    kept[n] = NULL;
    environ = kept;
}

/* 1 when path is under root/ (or equals root). */
static int hush_sandbox_under(const char *root, const char *path)
{
    size_t n = 0;

    if (root == NULL || path == NULL || root[0] == '\0')
        return 0;
    n = strlen(root);
    if (strcmp(root, path) == 0)
        return 1;
    return strncmp(path, root, n) == 0 && path[n] == '/';
}

int hush_sandbox_auth_allowed(const char *path)
{
    char real[PATH_MAX];
    char home[PATH_MAX];
    const char *env_home = getenv("HOME");

    if (path == NULL || path[0] == '\0' || realpath(path, real) == NULL)
        return 0;
    if (env_home == NULL || realpath(env_home, home) == NULL)
        return 0;
    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(HUSH_SANDBOX_AUTH_FORBID); ++i) {
        char forbid[PATH_MAX];
        int n = snprintf(forbid, sizeof(forbid), "%s/%s", home,
                         HUSH_SANDBOX_AUTH_FORBID[i]);
        if (n > 0 && (size_t)n < sizeof(forbid) && hush_sandbox_under(forbid, real))
            return 0;
    }
    {
        char hush[HUSH_HOME_PATH_MAX];
        hush_home_root(hush, sizeof(hush));
        if (hush[0] != '\0' && hush_sandbox_under(hush, real))
            return 0;
    }
    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(HUSH_SANDBOX_AUTH_REL); ++i) {
        char allow[PATH_MAX];
        int n = snprintf(allow, sizeof(allow), "%s/%s", home, HUSH_SANDBOX_AUTH_REL[i]);
        if (n > 0 && (size_t)n < sizeof(allow) && hush_sandbox_under(allow, real))
            return 1;
    }
    return 0;
}

static hush_status_t hush_sandbox_add_deny(hush_sandbox_policy_t *policy, const char *path)
{
    char real[PATH_MAX];

    assert(policy != NULL && path != NULL);
    if (path[0] == '\0' || realpath(path, real) == NULL)
        return path[0] == '\0' || errno == ENOENT ? HUSH_OK : HUSH_ERR_DENIED;
    if (policy->ndeny >= HUSH_SANDBOX_DENY_MAX || strlen(real) >= HUSH_SANDBOX_PATH_MAX)
        return HUSH_ERR_DENIED;
    memcpy(policy->deny[policy->ndeny++], real, strlen(real) + 1);
    return HUSH_OK;
}

static hush_status_t hush_sandbox_add_auth(hush_sandbox_policy_t *policy, const char *path)
{
    char real[PATH_MAX];

    assert(policy != NULL && path != NULL);
    if (realpath(path, real) == NULL)
        return errno == ENOENT ? HUSH_OK : HUSH_ERR_DENIED;
    if (!hush_sandbox_auth_allowed(real)) {
        hush_sandbox_why_path("agent sandbox refused unknown auth path", real);
        return HUSH_ERR_DENIED;
    }
    if (policy->nauth >= HUSH_SANDBOX_AUTH_MAX || strlen(real) >= HUSH_SANDBOX_PATH_MAX)
        return HUSH_ERR_DENIED;
    memcpy(policy->auth_ro[policy->nauth++], real, strlen(real) + 1);
    return HUSH_OK;
}

/* mkdtemp under TMPDIR (or /tmp), mode 0700. */
static hush_status_t hush_sandbox_make_home(char *out, size_t outsz)
{
    const char *tmp = getenv("TMPDIR");
    char tmpl[HUSH_SANDBOX_PATH_MAX];
    int n = 0;

    assert(out != NULL && outsz > 0);
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    n = snprintf(tmpl, sizeof(tmpl), "%s/hush-job-XXXXXX", tmp);
    if (n <= 0 || (size_t)n >= sizeof(tmpl) || (size_t)n >= outsz)
        return HUSH_ERR_DENIED;
    if (mkdtemp(tmpl) == NULL)
        return HUSH_ERR_DENIED;
    if (chmod(tmpl, 0700) != 0) {
        (void)rmdir(tmpl);
        return HUSH_ERR_DENIED;
    }
    memcpy(out, tmpl, strlen(tmpl) + 1);
    return HUSH_OK;
}

/* Ensure parent dirs of stub/rel exist (mode 0700). */
static hush_status_t hush_sandbox_mkdir_parents(const char *stub, const char *rel)
{
    char path[HUSH_SANDBOX_PATH_MAX];
    char tmp[HUSH_SANDBOX_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", stub, rel);

    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_DENIED;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen(stub) + 1; *p != '\0'; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
            return HUSH_ERR_DENIED;
        *p = '/';
    }
    return HUSH_OK;
}

/* Symlink stub/rel -> real when real exists; record RO auth. */
static hush_status_t hush_sandbox_link_auth(hush_sandbox_policy_t *policy, const char *rel)
{
    char real[HUSH_SANDBOX_PATH_MAX];
    char linkpath[HUSH_SANDBOX_PATH_MAX];
    struct stat st;
    int n = 0;

    n = snprintf(real, sizeof(real), "%s/%s", policy->real_home, rel);
    if (n <= 0 || (size_t)n >= sizeof(real))
        return HUSH_ERR_DENIED;
    if (lstat(real, &st) != 0)
        return errno == ENOENT ? HUSH_OK : HUSH_ERR_DENIED;
    HUSH_TRY(hush_sandbox_add_auth(policy, real));
    HUSH_TRY(hush_sandbox_mkdir_parents(policy->stub_home, rel));
    n = snprintf(linkpath, sizeof(linkpath), "%s/%s", policy->stub_home, rel);
    if (n <= 0 || (size_t)n >= sizeof(linkpath))
        return HUSH_ERR_DENIED;
    if (symlink(real, linkpath) != 0 && errno != EEXIST)
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}

static hush_status_t hush_sandbox_prepare_auth(hush_sandbox_policy_t *policy)
{
    const char *extra = getenv("HUSH_SANDBOX_EXTRA_AUTH");

    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(HUSH_SANDBOX_AUTH_REL); ++i)
        HUSH_TRY(hush_sandbox_link_auth(policy, HUSH_SANDBOX_AUTH_REL[i]));
    if (extra != NULL && extra[0] != '\0')
        return hush_sandbox_add_auth(policy, extra);
    return HUSH_OK;
}

hush_status_t hush_sandbox_policy(hush_sandbox_policy_t *policy, const char *cwd,
                                  uint16_t relay_port)
{
    char path[HUSH_HOME_PATH_MAX];
    char real[PATH_MAX];
    const char *home = getenv("HOME");

    if (policy == NULL)
        return HUSH_ERR_ARG;
    memset(policy, 0, sizeof(*policy));
    /* Port 0 means no relay listener (unit tests): skip TCP connect rules. */
    policy->relay_port = relay_port;
    hush_sandbox_set_why("");
    if (home == NULL || home[0] == '\0' || realpath(home, real) == NULL) {
        hush_sandbox_set_why("agent sandbox refused: HOME is unset");
        return HUSH_ERR_DENIED;
    }
    memcpy(policy->real_home, real, strlen(real) + 1);
    if (hush_sandbox_make_home(policy->stub_home, sizeof(policy->stub_home)) != HUSH_OK) {
        hush_sandbox_set_why("agent sandbox refused: could not create job HOME");
        return HUSH_ERR_DENIED;
    }
    hush_home_root(path, sizeof(path));
    if (path[0] == '\0')
        return HUSH_ERR_DENIED;
    HUSH_TRY(hush_sandbox_add_deny(policy, path));
    hush_home_config_dir(path, sizeof(path));
    HUSH_TRY(hush_sandbox_add_deny(policy, path));
    hush_home_legacy_config_dir(path, sizeof(path));
    HUSH_TRY(hush_sandbox_add_deny(policy, path));
    /* Deny the operator HOME so ~/.ssh and friends stay out of the grant walk. */
    HUSH_TRY(hush_sandbox_add_deny(policy, policy->real_home));
    if (hush_sandbox_prepare_auth(policy) != HUSH_OK)
        return HUSH_ERR_DENIED;
    if (cwd != NULL && cwd[0] != '\0') {
        if (realpath(cwd, real) == NULL || strlen(real) >= HUSH_SANDBOX_PATH_MAX)
            return HUSH_ERR_DENIED;
        memcpy(policy->cwd, real, strlen(real) + 1);
    }
    return HUSH_OK;
}

static int hush_sandbox_is_denied(const hush_sandbox_policy_t *policy, const char *path)
{
    for (int i = 0; i < policy->ndeny; ++i) {
        if (strcmp(policy->deny[i], path) == 0)
            return 1;
    }
    return 0;
}

static int hush_sandbox_holds_denied(const hush_sandbox_policy_t *policy, const char *path)
{
    size_t n = strlen(path);

    for (int i = 0; i < policy->ndeny; ++i) {
        const char *d = policy->deny[i];
        if (n == 1 && path[0] == '/' && d[1] != '\0')
            return 1;
        if (strncmp(d, path, n) == 0 && d[n] == '/')
            return 1;
    }
    return 0;
}

static int hush_sandbox_bwrap_ok(void)
{
    const char *no = getenv("HUSH_SANDBOX_NO_BWRAP");
    char *path = NULL;

    if (no != NULL && no[0] != '\0' && strcmp(no, "0") != 0)
        return 0;
    path = getenv("PATH");
    if (path == NULL)
        return access("/usr/bin/bwrap", X_OK) == 0;
    {
        char buf[PATH_MAX];
        const char *p = path;
        while (*p != '\0') {
            const char *colon = strchr(p, ':');
            size_t n = colon != NULL ? (size_t)(colon - p) : strlen(p);
            if (n > 0 && n < sizeof(buf) - 16) {
                memcpy(buf, p, n);
                buf[n] = '\0';
                snprintf(buf + n, sizeof(buf) - n, "/bwrap");
                if (access(buf, X_OK) == 0)
                    return 1;
            }
            if (colon == NULL)
                break;
            p = colon + 1;
        }
    }
    return access("/usr/bin/bwrap", X_OK) == 0;
}

hush_sandbox_backend_t hush_sandbox_backend(void)
{
    const char *force = getenv("HUSH_SANDBOX_BACKEND");

    if (force != NULL) {
        if (strcmp(force, "landlock") == 0)
            return HUSH_SANDBOX_BACKEND_LANDLOCK;
        if (strcmp(force, "bwrap") == 0)
            return HUSH_SANDBOX_BACKEND_BWRAP;
        if (strcmp(force, "none") == 0)
            return HUSH_SANDBOX_BACKEND_NONE;
    }
    if (hush_sandbox_abi() >= HUSH_SANDBOX_ABI_NET)
        return HUSH_SANDBOX_BACKEND_LANDLOCK;
    if (hush_sandbox_bwrap_ok())
        return HUSH_SANDBOX_BACKEND_BWRAP;
    return HUSH_SANDBOX_BACKEND_NONE;
}

#if defined(__linux__)

#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER (1ULL << 13)
#endif
#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE (1ULL << 14)
#endif
#ifndef LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_ACCESS_FS_IOCTL_DEV (1ULL << 15)
#endif
#ifndef LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET
#define LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET (1ULL << 0)
#endif
#ifndef LANDLOCK_SCOPE_SIGNAL
#define LANDLOCK_SCOPE_SIGNAL (1ULL << 1)
#endif

#define HUSH_SANDBOX_FILE_RIGHTS \
    (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | \
     LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_TRUNCATE | \
     LANDLOCK_ACCESS_FS_IOCTL_DEV)

#define HUSH_SANDBOX_RO_RIGHTS \
    (LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)

typedef struct {
    uint64_t handled_access_fs;
    uint64_t handled_access_net;
    uint64_t scoped;
} hush_sandbox_attr_t;

typedef struct {
    int fd;
    uint64_t fs_all;
    uint64_t fs_file;
} hush_sandbox_rules_t;

int hush_sandbox_abi(void)
{
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0,
                       LANDLOCK_CREATE_RULESET_VERSION);
    return abi < 0 ? 0 : (int)abi;
}

static uint64_t hush_sandbox_fs_rights(int abi)
{
    uint64_t rights = (LANDLOCK_ACCESS_FS_MAKE_SYM << 1) - 1;

    if (abi >= 2)
        rights |= LANDLOCK_ACCESS_FS_REFER;
    if (abi >= 3)
        rights |= LANDLOCK_ACCESS_FS_TRUNCATE;
    if (abi >= 5)
        rights |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
    return rights;
}

static hush_status_t hush_sandbox_open_rules(hush_sandbox_rules_t *rules, int abi)
{
    hush_sandbox_attr_t attr = {0};
    size_t size = abi >= 6 ? sizeof(attr) : 2 * sizeof(uint64_t);

    assert(rules != NULL && abi >= HUSH_SANDBOX_ABI_NET);
    rules->fs_all = hush_sandbox_fs_rights(abi);
    rules->fs_file = rules->fs_all & HUSH_SANDBOX_FILE_RIGHTS;
    attr.handled_access_fs = rules->fs_all;
    attr.handled_access_net = LANDLOCK_ACCESS_NET_CONNECT_TCP;
    if (abi >= 6)
        attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL;
    rules->fd = (int)syscall(SYS_landlock_create_ruleset, &attr, size, 0U);
    return rules->fd < 0 ? HUSH_ERR_DENIED : HUSH_OK;
}

static hush_status_t hush_sandbox_grant(const hush_sandbox_rules_t *rules,
                                        const char *path, uint64_t access)
{
    struct landlock_path_beneath_attr beneath = {0};
    int fd = open(path, O_PATH | O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0)
        return (errno == ENOENT || errno == EACCES || errno == EPERM)
                   ? HUSH_OK : HUSH_ERR_DENIED;
    beneath.allowed_access = access;
    beneath.parent_fd = fd;
    long rc = syscall(SYS_landlock_add_rule, rules->fd, LANDLOCK_RULE_PATH_BENEATH,
                      &beneath, 0U);
    (void)close(fd);
    return rc == 0 ? HUSH_OK : HUSH_ERR_DENIED;
}

static hush_status_t hush_sandbox_grant_tree(const hush_sandbox_rules_t *rules,
                                             const hush_sandbox_policy_t *policy,
                                             const char *dir);

static hush_status_t hush_sandbox_grant_entry(const hush_sandbox_rules_t *rules,
                                              const hush_sandbox_policy_t *policy,
                                              const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0)
        return (errno == ENOENT || errno == EACCES || errno == EPERM)
                   ? HUSH_OK : HUSH_ERR_DENIED;
    if (S_ISLNK(st.st_mode) || hush_sandbox_is_denied(policy, path))
        return HUSH_OK;
    if (S_ISDIR(st.st_mode) && hush_sandbox_holds_denied(policy, path))
        return hush_sandbox_grant_tree(rules, policy, path);
    return hush_sandbox_grant(rules, path, S_ISDIR(st.st_mode) ? rules->fs_all
                                                               : rules->fs_file);
}

static hush_status_t hush_sandbox_grant_tree(const hush_sandbox_rules_t *rules,
                                             const hush_sandbox_policy_t *policy,
                                             const char *dir)
{
    char path[HUSH_SANDBOX_PATH_MAX];
    hush_status_t st = HUSH_OK;
    struct dirent *entry = NULL;
    DIR *d = opendir(dir);

    if (d == NULL)
        return (errno == EACCES || errno == EPERM) ? HUSH_OK : HUSH_ERR_DENIED;
    while (st == HUSH_OK && (entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        int n = snprintf(path, sizeof(path), "%s/%s", strcmp(dir, "/") == 0 ? "" : dir,
                         entry->d_name);
        st = n > 0 && (size_t)n < sizeof(path)
                 ? hush_sandbox_grant_entry(rules, policy, path) : HUSH_ERR_DENIED;
    }
    (void)closedir(d);
    return st;
}

static hush_status_t hush_sandbox_grant_cwd(const hush_sandbox_rules_t *rules,
                                            const hush_sandbox_policy_t *policy)
{
    const char *cwd = policy->cwd;

    if (cwd[0] == '\0' || hush_sandbox_is_denied(policy, cwd) ||
        hush_sandbox_holds_denied(policy, cwd))
        return HUSH_OK;
    return hush_sandbox_grant(rules, cwd, rules->fs_all);
}

static hush_status_t hush_sandbox_grant_ports(const hush_sandbox_rules_t *rules,
                                              uint16_t relay_port)
{
    struct landlock_net_port_attr net = {0};

    net.allowed_access = LANDLOCK_ACCESS_NET_CONNECT_TCP;
    for (uint32_t port = 1; port <= 65535U; ++port) {
        if (port == relay_port)
            continue;
        net.port = port;
        if (syscall(SYS_landlock_add_rule, rules->fd, LANDLOCK_RULE_NET_PORT, &net, 0U) != 0)
            return HUSH_ERR_DENIED;
    }
    return HUSH_OK;
}

static hush_status_t hush_sandbox_grant_auth(const hush_sandbox_rules_t *rules,
                                             const hush_sandbox_policy_t *policy)
{
    uint64_t ro = rules->fs_all & HUSH_SANDBOX_RO_RIGHTS;

    for (int i = 0; i < policy->nauth; ++i)
        HUSH_TRY(hush_sandbox_grant(rules, policy->auth_ro[i], ro));
    return HUSH_OK;
}

static hush_status_t hush_sandbox_landlock(const hush_sandbox_policy_t *policy)
{
    hush_sandbox_rules_t rules = {.fd = -1};
    int abi = hush_sandbox_abi();

    if (abi < HUSH_SANDBOX_ABI_NET)
        return HUSH_ERR_DENIED;
    HUSH_TRY(hush_sandbox_open_rules(&rules, abi));
    hush_status_t st = hush_sandbox_grant_tree(&rules, policy, "/");
    if (st == HUSH_OK)
        st = hush_sandbox_grant(&rules, "/", LANDLOCK_ACCESS_FS_READ_DIR);
    if (st == HUSH_OK)
        st = hush_sandbox_grant(&rules, policy->stub_home, rules.fs_all);
    if (st == HUSH_OK)
        st = hush_sandbox_grant_auth(&rules, policy);
    if (st == HUSH_OK)
        st = hush_sandbox_grant_cwd(&rules, policy);
    if (st == HUSH_OK && policy->relay_port != 0)
        st = hush_sandbox_grant_ports(&rules, policy->relay_port);
    if (st == HUSH_OK && prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        st = HUSH_ERR_DENIED;
    if (st == HUSH_OK && syscall(SYS_landlock_restrict_self, rules.fd, 0U) != 0)
        st = HUSH_ERR_DENIED;
    (void)close(rules.fd);
    return st;
}

#else /* !__linux__ */

int hush_sandbox_abi(void)
{
    return 0;
}

static hush_status_t hush_sandbox_landlock(const hush_sandbox_policy_t *policy)
{
    (void)policy;
    return HUSH_ERR_DENIED;
}

#endif

static void hush_sandbox_close_inherited(void)
{
    long max = sysconf(_SC_OPEN_MAX);

    if (max < 0 || max > 1L << 20)
        max = 1L << 20;
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0)
        return;
#endif
    for (long fd = 3; fd < max; ++fd)
        (void)close((int)fd);
}

hush_status_t hush_sandbox_enter(const hush_sandbox_policy_t *policy)
{
    if (policy == NULL || policy->ndeny < 1 || policy->stub_home[0] == '\0')
        return HUSH_ERR_DENIED;
    hush_sandbox_close_inherited();
    if (setenv("HOME", policy->stub_home, 1) != 0)
        return HUSH_ERR_DENIED;
    hush_sandbox_scrub_env();
    if (hush_sandbox_backend() != HUSH_SANDBOX_BACKEND_LANDLOCK)
        return HUSH_ERR_DENIED;
    return hush_sandbox_landlock(policy) == HUSH_OK ? HUSH_OK : HUSH_ERR_DENIED;
}

enum { HUSH_SANDBOX_BW_MAX = 128, HUSH_SANDBOX_BIND_MAX = 32 };

typedef struct {
    char **argv;
    int n;
} hush_sandbox_bw_t;

/* Pushes one word onto the bwrap argv. */
static int hush_sandbox_bw_push(hush_sandbox_bw_t *bw, const char *word)
{
    assert(bw != NULL);
    if (bw->n + 1 >= HUSH_SANDBOX_BW_MAX)
        return -1;
    bw->argv[bw->n++] = (char *)word;
    return 0;
}

/* --ro-bind src src for each existing system path (not /proc). */
static hush_status_t hush_sandbox_bw_sys(hush_sandbox_bw_t *bw)
{
    static const char *const sys_ro[] = {
        "/usr", "/workspace", "/lib", "/lib64", "/bin", "/sbin", "/etc",
        "/dev/null", "/dev/urandom", "/dev/random",
    };

    for (size_t i = 0; i < HUSH_SANDBOX_COUNT(sys_ro); ++i) {
        if (access(sys_ro[i], F_OK) != 0)
            continue;
        if (hush_sandbox_bw_push(bw, "--ro-bind") != 0 ||
            hush_sandbox_bw_push(bw, (char *)sys_ro[i]) != 0 ||
            hush_sandbox_bw_push(bw, (char *)sys_ro[i]) != 0)
            return HUSH_ERR_DENIED;
    }
    return HUSH_OK;
}

/* --ro-bind each auth tree into the stub HOME (replacing the symlink). */
static hush_status_t hush_sandbox_bw_auth(hush_sandbox_bw_t *bw,
                                          const hush_sandbox_policy_t *policy)
{
    static char bind_src[HUSH_SANDBOX_BIND_MAX][HUSH_SANDBOX_PATH_MAX];
    static char bind_dst[HUSH_SANDBOX_BIND_MAX][HUSH_SANDBOX_PATH_MAX];
    int nb = 0;

    for (int i = 0; i < policy->nauth && nb < HUSH_SANDBOX_BIND_MAX; ++i) {
        const char *src = policy->auth_ro[i];
        const char *rel = src + strlen(policy->real_home);
        int m = 0;

        if (rel[0] != '/')
            continue;
        rel++;
        m = snprintf(bind_dst[nb], sizeof(bind_dst[nb]), "%s/%s", policy->stub_home, rel);
        if (m <= 0 || (size_t)m >= sizeof(bind_dst[nb]))
            return HUSH_ERR_DENIED;
        (void)unlink(bind_dst[nb]);
        snprintf(bind_src[nb], sizeof(bind_src[nb]), "%s", src);
        if (hush_sandbox_bw_push(bw, "--ro-bind") != 0 ||
            hush_sandbox_bw_push(bw, bind_src[nb]) != 0 ||
            hush_sandbox_bw_push(bw, bind_dst[nb]) != 0)
            return HUSH_ERR_DENIED;
        nb++;
    }
    return HUSH_OK;
}

static hush_status_t hush_sandbox_bw_job(hush_sandbox_bw_t *bw,
                                         const hush_sandbox_policy_t *policy)
{
    if (hush_sandbox_bw_push(bw, "--bind") != 0 ||
        hush_sandbox_bw_push(bw, policy->stub_home) != 0 ||
        hush_sandbox_bw_push(bw, policy->stub_home) != 0)
        return HUSH_ERR_DENIED;
    HUSH_TRY(hush_sandbox_bw_auth(bw, policy));
    if (policy->cwd[0] != '\0') {
        if (hush_sandbox_bw_push(bw, "--bind") != 0 ||
            hush_sandbox_bw_push(bw, policy->cwd) != 0 ||
            hush_sandbox_bw_push(bw, policy->cwd) != 0 ||
            hush_sandbox_bw_push(bw, "--chdir") != 0 ||
            hush_sandbox_bw_push(bw, policy->cwd) != 0)
            return HUSH_ERR_DENIED;
    }
    if (hush_sandbox_bw_push(bw, "--setenv") != 0 ||
        hush_sandbox_bw_push(bw, "HOME") != 0 ||
        hush_sandbox_bw_push(bw, policy->stub_home) != 0)
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}


/* Resolves file to an absolute path using PATH when needed. */
static hush_status_t hush_sandbox_resolve(const char *file, char *out, size_t outsz)
{
    const char *path = NULL;
    char trial[PATH_MAX];

    assert(file != NULL && out != NULL && outsz > 0);
    if (strchr(file, '/') != NULL) {
        if (realpath(file, trial) == NULL || strlen(trial) >= outsz)
            return HUSH_ERR_DENIED;
        memcpy(out, trial, strlen(trial) + 1);
        return HUSH_OK;
    }
    path = getenv("PATH");
    if (path == NULL)
        return HUSH_ERR_DENIED;
    while (*path != '\0') {
        const char *colon = strchr(path, ':');
        size_t n = colon != NULL ? (size_t)(colon - path) : strlen(path);

        if (n > 0 && n + 1 + strlen(file) < sizeof(trial)) {
            memcpy(trial, path, n);
            trial[n] = '/';
            memcpy(trial + n + 1, file, strlen(file) + 1);
            if (access(trial, X_OK) == 0 && realpath(trial, out) != NULL &&
                strlen(out) < outsz)
                return HUSH_OK;
        }
        if (colon == NULL)
            break;
        path = colon + 1;
    }
    return HUSH_ERR_DENIED;
}

static hush_status_t hush_sandbox_bw_filedir(hush_sandbox_bw_t *bw, const char *file)
{
    static char dir[PATH_MAX];
    char resolved[PATH_MAX];
    char *slash = NULL;

    if (realpath(file, resolved) == NULL)
        return HUSH_OK;
    snprintf(dir, sizeof(dir), "%s", resolved);
    slash = strrchr(dir, '/');
    if (slash == NULL || slash == dir)
        return HUSH_OK;
    *slash = '\0';
    if (hush_sandbox_bw_push(bw, "--ro-bind") != 0 ||
        hush_sandbox_bw_push(bw, dir) != 0 ||
        hush_sandbox_bw_push(bw, dir) != 0)
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}

static hush_status_t hush_sandbox_bw_prelude(hush_sandbox_bw_t *bw, const char *absfile,
                                             const hush_sandbox_policy_t *policy)
{
    if (hush_sandbox_bw_push(bw, "bwrap") != 0 ||
        hush_sandbox_bw_push(bw, "--unshare-pid") != 0 ||
        hush_sandbox_bw_push(bw, "--unshare-net") != 0 ||
        hush_sandbox_bw_push(bw, "--proc") != 0 ||
        hush_sandbox_bw_push(bw, "/proc") != 0 ||
        hush_sandbox_bw_push(bw, "--die-with-parent") != 0 ||
        hush_sandbox_bw_push(bw, "--new-session") != 0)
        return HUSH_ERR_DENIED;
    HUSH_TRY(hush_sandbox_bw_sys(bw));
    HUSH_TRY(hush_sandbox_bw_job(bw, policy));
    HUSH_TRY(hush_sandbox_bw_filedir(bw, absfile));
    if (hush_sandbox_bw_push(bw, "--") != 0 || hush_sandbox_bw_push(bw, absfile) != 0)
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}

static hush_status_t hush_sandbox_exec_bwrap(const hush_sandbox_policy_t *policy,
                                             const char *file, char **argv)
{
    static char *words[HUSH_SANDBOX_BW_MAX];
    static char absfile[PATH_MAX];
    hush_sandbox_bw_t bw = {.argv = words, .n = 0};

    assert(policy != NULL && file != NULL && argv != NULL);
    if (!hush_sandbox_bwrap_ok()) {
        hush_sandbox_set_why("agent sandbox refused: bwrap not available");
        return HUSH_ERR_DENIED;
    }
#if defined(__linux__)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        hush_sandbox_set_why("agent sandbox refused: no_new_privs failed");
        return HUSH_ERR_DENIED;
    }
#endif
    if (hush_sandbox_resolve(file, absfile, sizeof(absfile)) != HUSH_OK) {
        hush_sandbox_set_why("agent sandbox refused: provider binary not found");
        return HUSH_ERR_DENIED;
    }
    HUSH_TRY(hush_sandbox_bw_prelude(&bw, absfile, policy));
    for (int i = 1; argv[i] != NULL; ++i) {
        if (hush_sandbox_bw_push(&bw, argv[i]) != 0)
            return HUSH_ERR_DENIED;
    }
    words[bw.n] = NULL;
    hush_sandbox_close_inherited();
    hush_sandbox_scrub_env();
    execvp("bwrap", words);
    hush_sandbox_set_why("agent sandbox refused: bwrap exec failed");
    return HUSH_ERR_IO;
}

hush_status_t hush_sandbox_exec(const char *cwd, uint16_t relay_port, const char *file,
                                char **argv)
{
    hush_sandbox_policy_t policy;
    hush_sandbox_backend_t backend = hush_sandbox_backend();

    if (file == NULL || argv == NULL)
        return HUSH_ERR_ARG;
    hush_sandbox_set_why("");
    if (backend == HUSH_SANDBOX_BACKEND_NONE) {
        hush_sandbox_set_why(
            "agent sandbox unavailable: need Landlock ABI>=4 or bwrap --unshare-pid");
        return HUSH_ERR_DENIED;
    }
    if (hush_sandbox_policy(&policy, cwd, relay_port) != HUSH_OK) {
        if (g_sandbox_why[0] == '\0')
            hush_sandbox_set_why("agent sandbox refused: policy setup failed");
        return HUSH_ERR_DENIED;
    }
    if (backend == HUSH_SANDBOX_BACKEND_LANDLOCK) {
        if (hush_sandbox_enter(&policy) != HUSH_OK) {
            hush_sandbox_set_why("agent sandbox refused: Landlock enter failed");
            return HUSH_ERR_DENIED;
        }
        execvp(file, argv);
        return HUSH_ERR_IO;
    }
    return hush_sandbox_exec_bwrap(&policy, file, argv);
}
