/* tests/test_sandbox_home.c: #336 B2. hush_sandbox_home_remove takes away
 * a job's stub HOME and nothing else:
 *   (a) a directory not named hush-job-* is kept;
 *   (b) a hush-job-* symlink is kept, and so is what it points at;
 *   (c) a real job HOME holding an auth-file symlink, a dir symlink and a
 *       nested link is removed, and every outside target survives;
 *   (d) an empty home, or a zero-size buffer, is a no-op.
 * Each removal call that runs empties home. A forked child with
 * RLIMIT_NPROC 0 makes hush_agent_spawn_grok's fork fail for real; the job
 * HOME it made before fork must be gone from the child's private TMPDIR. */

#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_agent_internal.h"
#include "hush_sandbox.h"

enum {
    SBH_PATH_MAX = 512,
    SBH_FTW_FDS = 16,
    /* Exit codes of the fork-failure child. */
    SBH_CHILD_OK = 0,
    SBH_CHILD_FORKED = 3,
    SBH_CHILD_SPAWN = 4,
    SBH_CHILD_SETUP = 5
};

#define SBH_DIR_TEMPLATE "/tmp/hush-sbhome-XXXXXX"
#define SBH_JOB_PREFIX "hush-job-"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Joins dir/name into out (SBH_PATH_MAX). */
static const char *join(char *out, const char *dir, const char *name)
{
    int n = snprintf(out, SBH_PATH_MAX, "%s/%s", dir, name);

    if (n <= 0 || n >= SBH_PATH_MAX) {
        fprintf(stderr, "FAIL path too long: %s/%s\n", dir, name);
        exit(1);
    }
    return out;
}

static void touch(const char *path)
{
    FILE *f = fopen(path, "w");

    expect(f != NULL, path);
    if (f != NULL)
        (void)fclose(f);
}

static int exists(const char *path)
{
    struct stat st = {0};

    return lstat(path, &st) == 0;
}

static int is_link(const char *path)
{
    struct stat st = {0};

    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

/* root/outside/keep and root/outside/d/g: the targets no removal may touch. */
static void make_outside(const char *root)
{
    char p[SBH_PATH_MAX];

    expect(mkdir(join(p, root, "outside"), 0700) == 0, "mkdir outside");
    touch(join(p, root, "outside/keep"));
    expect(mkdir(join(p, root, "outside/d"), 0700) == 0, "mkdir outside/d");
    touch(join(p, root, "outside/d/g"));
}

static void expect_outside(const char *root, const char *when)
{
    char p[SBH_PATH_MAX];

    if (!exists(join(p, root, "outside/keep"))) {
        fprintf(stderr, "FAIL %s: outside/keep removed\n", when);
        g_fail = 1;
    }
    if (!exists(join(p, root, "outside/d/g"))) {
        fprintf(stderr, "FAIL %s: outside/d/g removed\n", when);
        g_fail = 1;
    }
}

/* (a) not a hush-job-* dir: kept, home emptied. */
static void test_not_a_job(const char *root)
{
    char home[SBH_PATH_MAX];
    char p[SBH_PATH_MAX];
    char f[SBH_PATH_MAX];

    join(home, root, "not-a-job-XXXXXX");
    expect(mkdtemp(home) != NULL, "mkdtemp not-a-job");
    join(f, home, "f");
    touch(f);
    memcpy(p, home, sizeof(p));
    hush_sandbox_home_remove(home, sizeof(home));
    expect(exists(f), "(a) a non-job dir's file is kept");
    expect(exists(p), "(a) a non-job dir is kept");
    expect(home[0] == '\0', "(a) home emptied");
}

/* (b) a hush-job-* symlink to an outside dir: link and target kept. */
static void test_job_symlink(const char *root)
{
    char home[SBH_PATH_MAX];
    char p[SBH_PATH_MAX];
    char link[SBH_PATH_MAX];

    join(link, root, SBH_JOB_PREFIX "link");
    expect(symlink(join(p, root, "outside"), link) == 0, "symlink hush-job-link");
    memcpy(home, link, sizeof(home));
    hush_sandbox_home_remove(home, sizeof(home));
    expect(is_link(link), "(b) a hush-job-* symlink is kept");
    expect_outside(root, "(b)");
    expect(home[0] == '\0', "(b) home emptied");
}

/* Plants auth.json -> outside/keep, dirlink -> outside/d, .grok/x/up -> outside. */
static void plant_links(const char *home, const char *root)
{
    char p[SBH_PATH_MAX];
    char t[SBH_PATH_MAX];

    expect(symlink(join(t, root, "outside/keep"), join(p, home, "auth.json")) == 0,
           "symlink auth.json");
    expect(symlink(join(t, root, "outside/d"), join(p, home, "dirlink")) == 0,
           "symlink dirlink");
    expect(mkdir(join(p, home, ".grok"), 0700) == 0, "mkdir .grok");
    expect(mkdir(join(p, home, ".grok/x"), 0700) == 0, "mkdir .grok/x");
    expect(symlink(join(t, root, "outside"), join(p, home, ".grok/x/up")) == 0,
           "symlink .grok/x/up");
    touch(join(p, home, ".grok/x/own"));
}

/* (c) a real job HOME with links: removed, outside targets intact. */
static void test_job_home(const char *root)
{
    char home[SBH_PATH_MAX];
    char p[SBH_PATH_MAX];

    expect(setenv("TMPDIR", root, 1) == 0, "setenv TMPDIR");
    expect(hush_sandbox_home_make(home, sizeof(home)) == HUSH_OK, "home_make");
    expect(strncmp(home, root, strlen(root)) == 0, "home_make honours TMPDIR");
    plant_links(home, root);
    memcpy(p, home, sizeof(p));
    hush_sandbox_home_remove(home, sizeof(home));
    expect(!exists(p), "(c) the job HOME is removed");
    expect_outside(root, "(c)");
    expect(home[0] == '\0', "(c) home emptied");
}

/* (d) empty home and zero-size buffer: nothing removed. */
static void test_noop(const char *root)
{
    char home[SBH_PATH_MAX] = {0};
    char p[SBH_PATH_MAX];

    hush_sandbox_home_remove(home, sizeof(home));
    expect(home[0] == '\0', "(d) empty home stays empty");
    hush_sandbox_home_remove(NULL, sizeof(home));
    expect(setenv("TMPDIR", root, 1) == 0, "setenv TMPDIR");
    expect(hush_sandbox_home_make(home, sizeof(home)) == HUSH_OK, "home_make (d)");
    memcpy(p, home, sizeof(p));
    hush_sandbox_home_remove(home, 0);
    expect(exists(p), "(d) a zero-size buffer removes nothing");
    expect(strcmp(home, p) == 0, "(d) a zero-size buffer is left as is");
    hush_sandbox_home_remove(home, sizeof(home));
    expect(!exists(p), "(d) cleanup removes the job HOME");
}

/* Count of hush-job-* entries in dir, or -1 when it cannot be read. */
static int count_jobs(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e = NULL;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, SBH_JOB_PREFIX, strlen(SBH_JOB_PREFIX)) == 0)
            n++;
    (void)closedir(d);
    return n;
}

/* In the forked child: no more processes for this uid, then spawn. */
static int fork_fail_child(const char *tmp)
{
    struct rlimit none = {0, 0};
    hush_agent_job_t *job = calloc(1, sizeof(*job));
    pid_t probe;

    if (job == NULL || setenv("TMPDIR", tmp, 1) != 0 ||
        setrlimit(RLIMIT_NPROC, &none) != 0)
        return SBH_CHILD_SETUP;
    probe = fork();
    if (probe == 0)
        _exit(0);
    if (probe > 0)
        return SBH_CHILD_FORKED;
    job->fd = HUSH_AGENT_FD_NONE;
    if (hush_agent_spawn_grok(job) != HUSH_ERR_IO || job->stub_home[0] != '\0')
        return SBH_CHILD_SPAWN;
    return SBH_CHILD_OK;
}

/* P2-2: a real fork failure in hush_agent_spawn_grok leaves no job HOME. */
static void test_fork_failure(const char *root)
{
    char tmp[SBH_PATH_MAX];
    int status = 0;
    pid_t pid;

    expect(mkdir(join(tmp, root, "forkfail"), 0700) == 0, "mkdir forkfail");
    pid = fork();
    if (pid == 0)
        _exit(fork_fail_child(tmp));
    expect(pid > 0 && waitpid(pid, &status, 0) == pid, "wait fork-failure child");
    expect(WIFEXITED(status) && WEXITSTATUS(status) != SBH_CHILD_FORKED,
           "RLIMIT_NPROC 0 must make fork fail (running as root?)");
    expect(WIFEXITED(status) && WEXITSTATUS(status) != SBH_CHILD_SETUP,
           "fork-failure child setup");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == SBH_CHILD_OK,
           "spawn_grok returns HUSH_ERR_IO with home emptied on fork failure");
    expect(count_jobs(tmp) == 0, "fork failure leaves no hush-job-* in TMPDIR");
}

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
    (void)sb;
    (void)ftw;
    if (flag == FTW_DP)
        (void)rmdir(path);
    else
        (void)unlink(path);
    return 0;
}

int main(void)
{
    char root[] = SBH_DIR_TEMPLATE;

    if (mkdtemp(root) == NULL) {
        fprintf(stderr, "FAIL mkdtemp\n");
        return 1;
    }
    make_outside(root);
    test_not_a_job(root);
    test_job_symlink(root);
    test_job_home(root);
    test_noop(root);
    test_fork_failure(root);
    (void)nftw(root, rm_entry, SBH_FTW_FDS, FTW_DEPTH | FTW_PHYS);
    if (g_fail)
        return 1;
    printf("sandbox home: non-job kept, job symlink kept, job HOME removed with "
           "links unfollowed, no-op on empty, fork failure leaves no HOME OK\n");
    return 0;
}
