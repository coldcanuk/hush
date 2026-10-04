/* hush_keystore.c: reads and writes nsecs in pass, op, or secret-tool. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/crypto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_keystore.h"
#include "hush_pass.h"

#define HUSH_KEYSTORE_VAULT_DEFAULT "Hush"
#define HUSH_KEYSTORE_OP_PROG "op"
#define HUSH_KEYSTORE_SECRET_PROG "secret-tool"
#define HUSH_KEYSTORE_ATTR_SERVICE "service"
#define HUSH_KEYSTORE_ATTR_HUSH "hush"
#define HUSH_KEYSTORE_ATTR_ITEM "item"
#define HUSH_KEYSTORE_JSON_TITLE "hush-"

enum {
    HUSH_KEYSTORE_CMD_MAX = 512,
    HUSH_KEYSTORE_ITEM_MAX = 160,
    HUSH_KEYSTORE_URI_MAX = 288,
    HUSH_KEYSTORE_LABEL_MAX = 160,
    HUSH_KEYSTORE_JSON_MAX = 4096,
    HUSH_KEYSTORE_ESC_MAX = 2048,
    HUSH_KEYSTORE_ARGV_MAX = 12,
    HUSH_KEYSTORE_SINK_MAX = 256,
    HUSH_KEYSTORE_EXIT_MISSING = 127,
    HUSH_KEYSTORE_STORE_COUNT = 3
};

/* True when path is a relative store key of safe characters. */
static int hush_keystore_path_ok(const char *path);

/* Copies src into dst. Truncates to dstsz - 1. */
static void hush_keystore_copy(char *dst, size_t dstsz, const char *src);

/* True when prog is executable, by slash path or PATH search. */
static int hush_keystore_prog_ok(const char *prog);

/* True when dir/prog is executable. dirlen is the live prefix length. */
static int hush_keystore_dir_has(const char *dir, size_t dirlen,
                                 const char *prog);

/* Resolves the binary for kind into bin. 0 when that store is off. */
static int hush_keystore_bin(char *bin, size_t binsz, hush_keystore_kind kind);

/* Borrows the vault name. NULL when the name cannot sit in an op URI. */
static const char *hush_keystore_vault(void);

/* Writes the 1Password item title for path. */
static hush_status_t hush_keystore_item(char *out, size_t outsz,
                                       const char *path);

/* Escapes secret for a JSON string. 0 when it does not fit. */
static int hush_keystore_escape(const char *src, char *dst, size_t dstsz);

/* Builds the op item-create JSON document. */
static hush_status_t hush_keystore_op_json(char *out, size_t outsz,
                                          const char *item,
                                          const char *secret);

/* Strips one trailing CR or LF pair from a captured secret. */
static void hush_keystore_trim(char *text);

/* Opens the child stdin pipe and the child stdout pipe. */
static hush_status_t hush_keystore_open_pipes(int in_pipe[2], int out_pipe[2]);

/* Closes both pipe ends. */
static void hush_keystore_close_pipes(int in_pipe[2], int out_pipe[2]);

/* Child: stdin from the pipe, stdout captured, stderr discarded. */
static void hush_keystore_exec_child(int in_pipe[2], int out_pipe[2],
                                    char *const argv[]);

/* Writes every byte of text to fd. */
static hush_status_t hush_keystore_write_all(int fd, const char *text);

/* Discards leftover child stdout so the child does not die on SIGPIPE. */
static void hush_keystore_drain(int fd);

/* Reads child stdout into out. Bounds the read by outsz. */
static void hush_keystore_read_out(int fd, char *out, size_t outsz);

/* Parent half of a helper run. Waits even when the write fails. */
static hush_status_t hush_keystore_finish(int in_pipe[2], int out_pipe[2],
                                         pid_t pid, char *out, size_t outsz,
                                         const char *stdin_text);

/* Reports a failed fork after closing the pipes. */
static hush_status_t hush_keystore_fork_fail(void (*old_chld)(int),
                                            int in_pipe[2], int out_pipe[2]);

/* Runs argv. stdin_text may be NULL. Captures stdout into out. */
static hush_status_t hush_keystore_run(char *out, size_t outsz,
                                      const char *stdin_text,
                                      char *const argv[]);

/* pass adapter. */
static hush_status_t hush_keystore_pass_save(const char *path,
                                            const char *secret);

/* pass adapter. */
static hush_status_t hush_keystore_pass_load(char *out, size_t outsz,
                                            const char *path);

/* op adapter. Secret goes to the child only on stdin. */
static hush_status_t hush_keystore_op_save(const char *path,
                                          const char *secret);

/* op adapter. */
static hush_status_t hush_keystore_op_load(char *out, size_t outsz,
                                          const char *path);

/* secret-tool adapter. */
static hush_status_t hush_keystore_secret_save(const char *path,
                                              const char *secret);

/* secret-tool adapter. */
static hush_status_t hush_keystore_secret_load(char *out, size_t outsz,
                                              const char *path);

int hush_keystore_ready(hush_keystore_kind kind)
{
    char bin[HUSH_KEYSTORE_CMD_MAX];

    if (kind == HUSH_KEYSTORE_PASS)
        return hush_pass_available();
    return hush_keystore_bin(bin, sizeof(bin), kind);
}

hush_status_t hush_keystore_save(hush_keystore_kind kind, const char *path,
                                 const char *secret)
{
    if (!hush_keystore_path_ok(path) || secret == NULL || secret[0] == '\0')
        return HUSH_ERR_ARG;
    if (!hush_keystore_ready(kind))
        return HUSH_ERR_IO;
    if (kind == HUSH_KEYSTORE_PASS)
        return hush_keystore_pass_save(path, secret);
    if (kind == HUSH_KEYSTORE_OP)
        return hush_keystore_op_save(path, secret);
    if (kind == HUSH_KEYSTORE_SECRET)
        return hush_keystore_secret_save(path, secret);
    return HUSH_ERR_ARG;
}

void hush_keystore_offer(int use_pass, const char *path, const char *secret)
{
    if (!hush_keystore_path_ok(path) || secret == NULL || secret[0] == '\0')
        return;
    if (use_pass) {
        (void)hush_keystore_save(HUSH_KEYSTORE_PASS, path, secret);
        return;
    }
    if (hush_keystore_ready(HUSH_KEYSTORE_OP)) {
        (void)hush_keystore_save(HUSH_KEYSTORE_OP, path, secret);
        return;
    }
    if (hush_keystore_ready(HUSH_KEYSTORE_SECRET))
        (void)hush_keystore_save(HUSH_KEYSTORE_SECRET, path, secret);
}

hush_status_t hush_keystore_load_kind(hush_keystore_kind kind, char *out,
                                     size_t outsz, const char *path)
{
    hush_status_t st;

    if (out == NULL || outsz == 0 || !hush_keystore_path_ok(path))
        return HUSH_ERR_ARG;
    out[0] = '\0';
    if (!hush_keystore_ready(kind))
        return HUSH_ERR_NOT_FOUND;
    if (kind == HUSH_KEYSTORE_PASS)
        st = hush_keystore_pass_load(out, outsz, path);
    else if (kind == HUSH_KEYSTORE_OP)
        st = hush_keystore_op_load(out, outsz, path);
    else if (kind == HUSH_KEYSTORE_SECRET)
        st = hush_keystore_secret_load(out, outsz, path);
    else
        return HUSH_ERR_ARG;
    if (st != HUSH_OK) {
        OPENSSL_cleanse(out, outsz);
        return HUSH_ERR_NOT_FOUND;
    }
    hush_keystore_trim(out);
    if (out[0] == '\0')
        return HUSH_ERR_NOT_FOUND;
    return HUSH_OK;
}

hush_status_t hush_keystore_load(char *out, size_t outsz, const char *path)
{
    return hush_keystore_load_from(NULL, out, outsz, path);
}

hush_status_t hush_keystore_load_from(hush_keystore_kind *found, char *out,
                                     size_t outsz, const char *path)
{
    static const hush_keystore_kind order[HUSH_KEYSTORE_STORE_COUNT] = {
        HUSH_KEYSTORE_PASS,
        HUSH_KEYSTORE_OP,
        HUSH_KEYSTORE_SECRET
    };
    size_t i;

    if (out == NULL || outsz == 0 || !hush_keystore_path_ok(path))
        return HUSH_ERR_ARG;
    if (found != NULL)
        *found = HUSH_KEYSTORE_NONE;
    out[0] = '\0';
    for (i = 0; i < (size_t)HUSH_KEYSTORE_STORE_COUNT; i++) {
        hush_status_t st;

        st = hush_keystore_load_kind(order[i], out, outsz, path);
        if (st == HUSH_OK) {
            if (found != NULL)
                *found = order[i];
            return HUSH_OK;
        }
        OPENSSL_cleanse(out, outsz);
    }
    out[0] = '\0';
    return HUSH_ERR_NOT_FOUND;
}

hush_status_t hush_keystore_import(hush_identity_t *id, const char *path)
{
    char secret[HUSH_PASS_SECRET_MAX];
    hush_status_t st;

    if (id == NULL)
        return HUSH_ERR_ARG;
    hush_identity_clear(id);
    memset(secret, 0, sizeof(secret));
    st = hush_keystore_load(secret, sizeof(secret), path);
    if (st != HUSH_OK)
        return st;
    st = hush_identity_import(id, secret);
    OPENSSL_cleanse(secret, sizeof(secret));
    if (st != HUSH_OK)
        hush_identity_clear(id);
    return st;
}

static int hush_keystore_path_ok(const char *path)
{
    size_t i;
    size_t n;

    if (path == NULL || path[0] == '\0')
        return 0;
    n = strlen(path);
    if (n == 0 || n >= (size_t)HUSH_PASS_PATH_MAX)
        return 0;
    if (path[0] == '/' || path[0] == '.')
        return 0;
    if (strstr(path, "..") != NULL)
        return 0;
    for (i = 0; i < n; i++) {
        char c = path[i];
        int letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        int digit = (c >= '0' && c <= '9');

        if (!letter && !digit && c != '/' && c != '-' && c != '_')
            return 0;
    }
    return 1;
}

static void hush_keystore_copy(char *dst, size_t dstsz, const char *src)
{
    size_t n;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n + 1 > dstsz)
        n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int hush_keystore_dir_has(const char *dir, size_t dirlen,
                                 const char *prog)
{
    char full[HUSH_KEYSTORE_CMD_MAX];

    assert(dir != NULL);
    assert(prog != NULL);
    if (dirlen == 0)
        return 0;
    if (dirlen + strlen(prog) + 2 > sizeof(full))
        return 0;
    memcpy(full, dir, dirlen);
    full[dirlen] = '/';
    memcpy(full + dirlen + 1, prog, strlen(prog) + 1);
    return access(full, X_OK) == 0;
}

static int hush_keystore_prog_ok(const char *prog)
{
    const char *path = NULL;
    const char *cur = NULL;

    assert(prog != NULL);
    if (prog[0] == '\0')
        return 0;
    if (strchr(prog, '/') != NULL)
        return access(prog, X_OK) == 0;
    path = getenv("PATH");
    if (path == NULL || path[0] == '\0')
        return access(prog, X_OK) == 0;
    cur = path;
    /* Bounded walk: each step consumes one PATH entry. */
    while (cur[0] != '\0' || path[0] != '\0') {
        const char *end = strchr(cur, ':');
        size_t dirlen = (end != NULL) ? (size_t)(end - cur) : strlen(cur);

        if (hush_keystore_dir_has(cur, dirlen, prog))
            return 1;
        if (end == NULL)
            return 0;
        cur = end + 1;
        if (cur == path)
            return 0;
    }
    return 0;
}

static int hush_keystore_bin(char *bin, size_t binsz, hush_keystore_kind kind)
{
    const char *env_name = NULL;
    const char *prog = NULL;
    const char *env = NULL;

    assert(bin != NULL);
    assert(binsz > 0);
    bin[0] = '\0';
    if (kind == HUSH_KEYSTORE_OP) {
        env_name = HUSH_KEYSTORE_ENV_OP;
        prog = HUSH_KEYSTORE_OP_PROG;
    } else if (kind == HUSH_KEYSTORE_SECRET) {
        env_name = HUSH_KEYSTORE_ENV_SECRET;
        prog = HUSH_KEYSTORE_SECRET_PROG;
    } else
        return 0;
    env = getenv(env_name);
    if (env != NULL && env[0] != '\0') {
        if (!hush_keystore_prog_ok(env))
            return 0;
        hush_keystore_copy(bin, binsz, env);
        return bin[0] != '\0';
    }
    if (hush_pass_uses_override())
        return 0;
    if (!hush_keystore_prog_ok(prog))
        return 0;
    hush_keystore_copy(bin, binsz, prog);
    return 1;
}

static const char *hush_keystore_vault(void)
{
    const char *vault = NULL;

    vault = getenv(HUSH_KEYSTORE_ENV_VAULT);
    if (vault == NULL || vault[0] == '\0')
        return HUSH_KEYSTORE_VAULT_DEFAULT;
    if (strchr(vault, '/') != NULL || strchr(vault, ' ') != NULL)
        return NULL;
    if (strchr(vault, ':') != NULL)
        return NULL;
    return vault;
}

static hush_status_t hush_keystore_item(char *out, size_t outsz,
                                       const char *path)
{
    size_t i;
    size_t prefix;

    assert(out != NULL);
    assert(path != NULL);
    prefix = strlen(HUSH_KEYSTORE_JSON_TITLE);
    if (prefix + strlen(path) + 1 > outsz)
        return HUSH_ERR_ARG;
    memcpy(out, HUSH_KEYSTORE_JSON_TITLE, prefix);
    for (i = 0; path[i] != '\0'; i++) {
        char c = path[i];

        if (c == '/')
            c = '-';
        out[prefix + i] = c;
    }
    out[prefix + i] = '\0';
    return HUSH_OK;
}

static int hush_keystore_escape(const char *src, char *dst, size_t dstsz)
{
    size_t used = 0;
    size_t i;
    size_t n;

    assert(src != NULL);
    assert(dst != NULL);
    assert(dstsz > 0);
    n = strlen(src);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        char tiny[8];
        const char *add = NULL;
        size_t addlen;

        if (c == '"' || c == '\\') {
            tiny[0] = '\\';
            tiny[1] = (char)c;
            tiny[2] = '\0';
            add = tiny;
        } else if (c == '\n') {
            add = "\\n";
        } else if (c < 0x20) {
            return 0;
        } else {
            tiny[0] = (char)c;
            tiny[1] = '\0';
            add = tiny;
        }
        addlen = strlen(add);
        if (used + addlen + 1 > dstsz)
            return 0;
        memcpy(dst + used, add, addlen);
        used += addlen;
    }
    dst[used] = '\0';
    return 1;
}

static hush_status_t hush_keystore_op_json(char *out, size_t outsz,
                                          const char *item,
                                          const char *secret)
{
    char escaped[HUSH_KEYSTORE_ESC_MAX];
    int wrote;

    assert(out != NULL);
    assert(item != NULL);
    assert(secret != NULL);
    if (!hush_keystore_escape(secret, escaped, sizeof(escaped)))
        return HUSH_ERR_ARG;
    wrote = snprintf(out, outsz,
                     "{\"title\":\"%s\",\"category\":\"PASSWORD\","
                     "\"fields\":[{\"id\":\"password\",\"type\":\"CONCEALED\","
                     "\"purpose\":\"PASSWORD\",\"label\":\"password\","
                     "\"value\":\"%s\"}]}",
                     item, escaped);
    OPENSSL_cleanse(escaped, sizeof(escaped));
    if (wrote < 0 || (size_t)wrote >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

static void hush_keystore_trim(char *text)
{
    size_t n;

    assert(text != NULL);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        text[n - 1] = '\0';
        n--;
    }
}

static hush_status_t hush_keystore_open_pipes(int in_pipe[2], int out_pipe[2])
{
    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    if (pipe(in_pipe) != 0)
        return HUSH_ERR_IO;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}

static void hush_keystore_close_pipes(int in_pipe[2], int out_pipe[2])
{
    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    close(in_pipe[0]);
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(out_pipe[1]);
}

static void hush_keystore_exec_child(int in_pipe[2], int out_pipe[2],
                                    char *const argv[])
{
    int discarded;

    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    assert(argv != NULL);
    close(in_pipe[1]);
    close(out_pipe[0]);
    if (dup2(in_pipe[0], STDIN_FILENO) < 0)
        _exit(HUSH_KEYSTORE_EXIT_MISSING);
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0)
        _exit(HUSH_KEYSTORE_EXIT_MISSING);
    discarded = open("/dev/null", O_WRONLY);
    if (discarded < 0)
        _exit(HUSH_KEYSTORE_EXIT_MISSING);
    if (dup2(discarded, STDERR_FILENO) < 0)
        _exit(HUSH_KEYSTORE_EXIT_MISSING);
    if (discarded != STDERR_FILENO)
        close(discarded);
    close(in_pipe[0]);
    close(out_pipe[1]);
    execvp(argv[0], argv);
    _exit(HUSH_KEYSTORE_EXIT_MISSING);
}

static hush_status_t hush_keystore_write_all(int fd, const char *text)
{
    size_t want;
    size_t sent = 0;

    if (text == NULL)
        return HUSH_OK;
    want = strlen(text);
    while (sent < want) {
        ssize_t wrote = write(fd, text + sent, want - sent);

        if (wrote <= 0)
            return HUSH_ERR_IO;
        sent += (size_t)wrote;
    }
    return HUSH_OK;
}

static void hush_keystore_drain(int fd)
{
    char sink[HUSH_KEYSTORE_SINK_MAX];
    ssize_t got;

    do {
        got = read(fd, sink, sizeof(sink));
    } while (got > 0);
    OPENSSL_cleanse(sink, sizeof(sink));
}

static void hush_keystore_read_out(int fd, char *out, size_t outsz)
{
    size_t nread = 0;

    if (out == NULL || outsz == 0) {
        hush_keystore_drain(fd);
        return;
    }
    while (nread + 1 < outsz) {
        ssize_t got = read(fd, out + nread, outsz - 1 - nread);

        if (got <= 0)
            break;
        nread += (size_t)got;
    }
    out[nread] = '\0';
    if (nread + 1 >= outsz)
        hush_keystore_drain(fd);
}

static hush_status_t hush_keystore_finish(int in_pipe[2], int out_pipe[2],
                                         pid_t pid, char *out, size_t outsz,
                                         const char *stdin_text)
{
    hush_status_t st;
    int status = 0;

    assert(in_pipe != NULL);
    assert(out_pipe != NULL);
    close(in_pipe[0]);
    close(out_pipe[1]);
    st = hush_keystore_write_all(in_pipe[1], stdin_text);
    close(in_pipe[1]);
    hush_keystore_read_out(out_pipe[0], out, outsz);
    close(out_pipe[0]);
    if (waitpid(pid, &status, 0) < 0)
        return HUSH_ERR_IO;
    if (st != HUSH_OK)
        return st;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_keystore_fork_fail(void (*old_chld)(int),
                                            int in_pipe[2], int out_pipe[2])
{
    if (old_chld != SIG_ERR)
        signal(SIGCHLD, old_chld);
    hush_keystore_close_pipes(in_pipe, out_pipe);
    return HUSH_ERR_IO;
}

static hush_status_t hush_keystore_run(char *out, size_t outsz,
                                      const char *stdin_text,
                                      char *const argv[])
{
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    hush_status_t st;
    void (*old_chld)(int);

    assert(argv != NULL);
    assert(argv[0] != NULL);
    st = hush_keystore_open_pipes(in_pipe, out_pipe);
    if (st != HUSH_OK)
        return st;
    old_chld = signal(SIGCHLD, SIG_DFL);
    pid = fork();
    if (pid < 0)
        return hush_keystore_fork_fail(old_chld, in_pipe, out_pipe);
    if (pid == 0)
        hush_keystore_exec_child(in_pipe, out_pipe, argv);
    st = hush_keystore_finish(in_pipe, out_pipe, pid, out, outsz, stdin_text);
    if (old_chld != SIG_ERR)
        signal(SIGCHLD, old_chld);
    return st;
}

static hush_status_t hush_keystore_pass_save(const char *path,
                                            const char *secret)
{
    assert(path != NULL);
    assert(secret != NULL);
    return hush_pass_save(path, secret);
}

static hush_status_t hush_keystore_pass_load(char *out, size_t outsz,
                                            const char *path)
{
    assert(out != NULL);
    assert(path != NULL);
    return hush_pass_get(out, outsz, path);
}

static hush_status_t hush_keystore_op_save(const char *path,
                                          const char *secret)
{
    char bin[HUSH_KEYSTORE_CMD_MAX];
    char item[HUSH_KEYSTORE_ITEM_MAX];
    char json[HUSH_KEYSTORE_JSON_MAX];
    const char *vault = NULL;
    char *argv[HUSH_KEYSTORE_ARGV_MAX];
    hush_status_t st;

    assert(path != NULL);
    assert(secret != NULL);
    if (!hush_keystore_bin(bin, sizeof(bin), HUSH_KEYSTORE_OP))
        return HUSH_ERR_IO;
    vault = hush_keystore_vault();
    if (vault == NULL)
        return HUSH_ERR_ARG;
    if (hush_keystore_item(item, sizeof(item), path) != HUSH_OK)
        return HUSH_ERR_ARG;
    st = hush_keystore_op_json(json, sizeof(json), item, secret);
    if (st != HUSH_OK)
        return st;
    argv[0] = bin;
    argv[1] = (char *)"item";
    argv[2] = (char *)"create";
    argv[3] = (char *)"--vault";
    argv[4] = (char *)vault;
    argv[5] = (char *)"--title";
    argv[6] = item;
    argv[7] = (char *)"-";
    argv[8] = NULL;
    st = hush_keystore_run(NULL, 0, json, argv);
    OPENSSL_cleanse(json, sizeof(json));
    return st;
}

static hush_status_t hush_keystore_op_load(char *out, size_t outsz,
                                          const char *path)
{
    char bin[HUSH_KEYSTORE_CMD_MAX];
    char item[HUSH_KEYSTORE_ITEM_MAX];
    char uri[HUSH_KEYSTORE_URI_MAX];
    const char *vault = NULL;
    char *argv[HUSH_KEYSTORE_ARGV_MAX];
    int wrote;

    assert(out != NULL);
    assert(path != NULL);
    if (!hush_keystore_bin(bin, sizeof(bin), HUSH_KEYSTORE_OP))
        return HUSH_ERR_IO;
    vault = hush_keystore_vault();
    if (vault == NULL)
        return HUSH_ERR_ARG;
    if (hush_keystore_item(item, sizeof(item), path) != HUSH_OK)
        return HUSH_ERR_ARG;
    wrote = snprintf(uri, sizeof(uri), "op://%s/%s/password", vault, item);
    if (wrote < 0 || (size_t)wrote >= sizeof(uri))
        return HUSH_ERR_ARG;
    argv[0] = bin;
    argv[1] = (char *)"read";
    argv[2] = uri;
    argv[3] = NULL;
    return hush_keystore_run(out, outsz, NULL, argv);
}

static hush_status_t hush_keystore_secret_save(const char *path,
                                              const char *secret)
{
    char bin[HUSH_KEYSTORE_CMD_MAX];
    char label[HUSH_KEYSTORE_LABEL_MAX];
    char *argv[HUSH_KEYSTORE_ARGV_MAX];
    int wrote;

    assert(path != NULL);
    assert(secret != NULL);
    if (!hush_keystore_bin(bin, sizeof(bin), HUSH_KEYSTORE_SECRET))
        return HUSH_ERR_IO;
    wrote = snprintf(label, sizeof(label), "Hush %s", path);
    if (wrote < 0 || (size_t)wrote >= sizeof(label))
        return HUSH_ERR_ARG;
    argv[0] = bin;
    argv[1] = (char *)"store";
    argv[2] = (char *)"--label";
    argv[3] = label;
    argv[4] = (char *)HUSH_KEYSTORE_ATTR_SERVICE;
    argv[5] = (char *)HUSH_KEYSTORE_ATTR_HUSH;
    argv[6] = (char *)HUSH_KEYSTORE_ATTR_ITEM;
    argv[7] = (char *)path;
    argv[8] = NULL;
    return hush_keystore_run(NULL, 0, secret, argv);
}

static hush_status_t hush_keystore_secret_load(char *out, size_t outsz,
                                              const char *path)
{
    char bin[HUSH_KEYSTORE_CMD_MAX];
    char *argv[HUSH_KEYSTORE_ARGV_MAX];

    assert(out != NULL);
    assert(path != NULL);
    if (!hush_keystore_bin(bin, sizeof(bin), HUSH_KEYSTORE_SECRET))
        return HUSH_ERR_IO;
    argv[0] = bin;
    argv[1] = (char *)"lookup";
    argv[2] = (char *)HUSH_KEYSTORE_ATTR_SERVICE;
    argv[3] = (char *)HUSH_KEYSTORE_ATTR_HUSH;
    argv[4] = (char *)HUSH_KEYSTORE_ATTR_ITEM;
    argv[5] = (char *)path;
    argv[6] = NULL;
    return hush_keystore_run(out, outsz, NULL, argv);
}
