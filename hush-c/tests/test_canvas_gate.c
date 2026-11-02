/* tests/test_canvas_gate.c: #356. The canvas Grok run never auto-approves:
 * its argv carries dontAsk with Bash, MCPTool, WebFetch, WebSearch and Edit
 * denied, and no --always-approve. It starts only after Grok's readiness
 * probes pass; a stub reporting an old version gets probed and never runs a
 * prompt. A stub grok under a scratch TMPDIR answers; no real CLI runs. */

#define _XOPEN_SOURCE 700

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hush_canvas.h"

enum {
    CN_PATH_MAX = 512,
    CN_BODY_MAX = 2048,
    CN_READ_MAX = 16384,
    CN_POLL_STEPS = 400,
    CN_POLL_NS = 50000000,
    CN_FTW_FDS = 16
};

#define CN_HELP "--permission-mode dontAsk --allow --deny --disallowed-tools"

/* One run: the failure flag and the scratch tree. Nothing here is static. */
typedef struct {
    int fail;
    char root[CN_PATH_MAX];
    char argv_file[CN_PATH_MAX + 32];
    char probe_file[CN_PATH_MAX + 32];
} cn_t;

static const char *const cn_denied[] = {
    "\n--deny\nBash\n", "\n--deny\nMCPTool\n", "\n--deny\nWebFetch\n",
    "\n--deny\nWebSearch\n", "\n--deny\nEdit\n"
};

static void expect(cn_t *t, int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        t->fail = 1;
    }
}

/* Makes <root>/<rel> (mode 0700) into out. */
static void make_dir(cn_t *t, char *out, size_t outsz, const char *rel)
{
    snprintf(out, outsz, "%s/%s", t->root, rel);
    expect(t, mkdir(out, S_IRWXU) == 0, rel);
}

/* <root>/bin/grok: probes log $1 and answer with version; a prompt run
 * writes its argv, one entry per line, and prints a completion. */
static void stub(cn_t *t, const char *version)
{
    char path[CN_PATH_MAX + 32] = {0};
    char body[CN_BODY_MAX] = {0};
    FILE *f = NULL;

    snprintf(body, sizeof(body), "#!/bin/sh\ncase \"$1\" in\n"
             "--version) echo \"$1\" >> '%s'; echo 'grok %s'; exit 0;;\n"
             "--help) echo \"$1\" >> '%s'; echo '%s'; exit 0;;\nesac\n"
             "printf '%%s\\n' \"$@\" > '%s'\nprintf 'int x;\\n'\n",
             t->probe_file, version, t->probe_file, CN_HELP, t->argv_file);
    snprintf(path, sizeof(path), "%s/bin/grok", t->root);
    f = fopen(path, "w");
    expect(t, f != NULL && fputs(body, f) >= 0, "write stub");
    if (f != NULL)
        fclose(f);
    expect(t, chmod(path, S_IRWXU) == 0, "stub mode");
}

/* <TMPDIR>/hush-canvas-gate-XXXXXX/{user/.grok/auth.json, home, bin}. */
static int fixture(cn_t *t)
{
    const char *tmp = getenv("TMPDIR");
    char p[CN_PATH_MAX + 32] = {0};
    FILE *f = NULL;

    snprintf(t->root, sizeof(t->root), "%s/hush-canvas-gate-XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
    if (mkdtemp(t->root) == NULL)
        return 0;
    make_dir(t, p, sizeof(p), "user");
    expect(t, setenv("HOME", p, 1) == 0, "HOME");
    make_dir(t, p, sizeof(p), "user/.grok");
    snprintf(p, sizeof(p), "%s/user/.grok/auth.json", t->root);
    f = fopen(p, "w");
    expect(t, f != NULL && fputs("{\"ok\":true}\n", f) >= 0 && fclose(f) == 0, "auth.json");
    make_dir(t, p, sizeof(p), "home");
    expect(t, setenv("HUSH_HOME", p, 1) == 0, "HUSH_HOME");
    make_dir(t, p, sizeof(p), "bin");
    snprintf(t->argv_file, sizeof(t->argv_file), "%s/bin/argv", t->root);
    snprintf(t->probe_file, sizeof(t->probe_file), "%s/bin/probes", t->root);
    snprintf(p, sizeof(p), "%s/bin:/usr/bin:/bin", t->root);
    expect(t, setenv("PATH", p, 1) == 0, "PATH");
    return 1;
}

/* Reads path into buf after a leading newline; 0 when it is not there. */
static int slurp(const char *path, char *buf, size_t bufsz)
{
    FILE *f = fopen(path, "r");

    buf[0] = '\0';
    if (f == NULL)
        return 0;
    buf[0] = '\n';
    size_t n = fread(buf + 1, 1, bufsz - 2, f);

    fclose(f);
    buf[n + 1] = '\0';
    return 1;
}

/* Starts one canvas job and polls it until it ends. */
static void run_job(cn_t *t)
{
    char token[HUSH_CANVAS_TOKEN_MAX] = {0};
    struct timespec ns = {0, CN_POLL_NS};

    expect(t, hush_canvas_start(token, sizeof(token), "int ", ";") == HUSH_OK, "start");
    for (int i = 0; i < CN_POLL_STEPS && hush_canvas_is_busy(token); ++i) {
        hush_canvas_poll();
        (void)nanosleep(&ns, NULL);
    }
    expect(t, hush_canvas_is_busy(token) == 0, "the job finishes");
}

/* A ready Grok runs once, after both probes, under the deny policy. */
static void check_ready(cn_t *t, char *buf)
{
    stub(t, "0.2.50");
    run_job(t);
    expect(t, slurp(t->argv_file, buf, CN_READ_MAX), "a ready grok ran the prompt");
    expect(t, strstr(buf, "\n--always-approve\n") == NULL, "canvas: never --always-approve");
    expect(t, strstr(buf, "\n--permission-mode\ndontAsk\n") != NULL, "canvas: dontAsk");
    expect(t, strstr(buf, "\n--allow\nEdit\n") == NULL, "canvas: Edit never allowed");
    for (size_t i = 0; i < sizeof(cn_denied) / sizeof(cn_denied[0]); i++)
        expect(t, strstr(buf, cn_denied[i]) != NULL, cn_denied[i]);
    expect(t, slurp(t->probe_file, buf, CN_READ_MAX) && strstr(buf, "\n--version\n") != NULL &&
           strstr(buf, "\n--help\n") != NULL, "canvas: both readiness probes ran");
}

/* An old Grok is probed and never runs a prompt. */
static void check_old(cn_t *t, char *buf)
{
    expect(t, unlink(t->argv_file) == 0 && unlink(t->probe_file) == 0, "reset logs");
    stub(t, "0.2.49");
    run_job(t);
    expect(t, !slurp(t->argv_file, buf, CN_READ_MAX), "an old grok runs no prompt");
    expect(t, slurp(t->probe_file, buf, CN_READ_MAX) && strstr(buf, "\n--version\n") != NULL,
           "the version probe ran");
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
    (void)sb;
    (void)flag;
    (void)ftw;
    return remove(path);
}

int main(void)
{
    cn_t t = {0};
    char *buf = malloc(CN_READ_MAX);

    if (buf == NULL || !fixture(&t)) {
        fprintf(stderr, "FAIL fixture\n");
        free(buf);
        return 1;
    }
    hush_canvas_init();
    check_ready(&t, buf);
    check_old(&t, buf);
    hush_canvas_shutdown();
    free(buf);
    expect(&t, nftw(t.root, rm_entry, CN_FTW_FDS, FTW_DEPTH | FTW_PHYS) == 0, "cleanup");
    if (t.fail)
        return 1;
    printf("canvas gate ok\n");
    return 0;
}
