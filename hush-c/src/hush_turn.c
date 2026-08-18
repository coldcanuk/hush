/* hush_turn.c: owns optional coturn STUN/TURN lifecycle for Hush. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hush_turn.h"

#if !defined(HUSH_STUN_TURN)
#define HUSH_STUN_TURN 1
#endif

#define HUSH_TURN_HOST_LOCAL "127.0.0.1"
#define HUSH_TURN_HEX "0123456789abcdef"

static const char *const hush_turn_bin_paths[] = {
    "/usr/bin/turnserver",
    "/usr/sbin/turnserver",
    "/usr/local/bin/turnserver",
    "/opt/homebrew/bin/turnserver",
    NULL
};

static const char *const hush_turn_unit_paths[] = {
    "/lib/systemd/system/hush-turn.service",
    "/etc/systemd/system/hush-turn.service",
    NULL
};

static void hush_turn_copy(char *dst, size_t dstsz, const char *src);
static void hush_turn_resolve_state(hush_turn_t *turn);
static void hush_turn_find_binary(hush_turn_t *turn);
static int hush_turn_unit_present(void);
static hush_status_t hush_turn_random_hex(char *out, size_t hex_chars);
static hush_status_t hush_turn_ensure_dir(const char *path);
static hush_status_t hush_turn_write_conf(const hush_turn_t *turn);
static hush_status_t hush_turn_spawn_child(hush_turn_t *turn);
static void hush_turn_kill_pid(pid_t pid);
static hush_status_t hush_turn_exec_systemctl(const char *verb);
static int hush_turn_alive(pid_t pid);

void hush_turn_init(hush_turn_t *turn)
{
    if (turn == NULL)
        return;
    memset(turn, 0, sizeof(*turn));
    turn->compiled_in = HUSH_STUN_TURN ? 1 : 0;
    turn->listen_port = (geteuid() == 0)
        ? (uint16_t)HUSH_TURN_PORT_PRIV
        : (uint16_t)HUSH_TURN_PORT_USER;
    hush_turn_copy(turn->public_host, sizeof(turn->public_host),
                   HUSH_TURN_HOST_LOCAL);
    hush_turn_copy(turn->realm, sizeof(turn->realm), HUSH_TURN_REALM_DEFAULT);
    hush_turn_copy(turn->username, sizeof(turn->username),
                   HUSH_TURN_USER_DEFAULT);
    hush_turn_resolve_state(turn);
    hush_turn_find_binary(turn);
    turn->have_unit = hush_turn_unit_present();
}

hush_status_t hush_turn_set_public_host(hush_turn_t *turn, const char *host)
{
    if (turn == NULL)
        return HUSH_ERR_ARG;
    if (host == NULL || host[0] == '\0')
        hush_turn_copy(turn->public_host, sizeof(turn->public_host),
                       HUSH_TURN_HOST_LOCAL);
    else
        hush_turn_copy(turn->public_host, sizeof(turn->public_host), host);
    return HUSH_OK;
}

hush_status_t hush_turn_enable(hush_turn_t *turn, hush_turn_mode_t mode)
{
    if (turn == NULL)
        return HUSH_ERR_ARG;
    if (!turn->compiled_in)
        return HUSH_ERR_NOT_FOUND;
    if (mode != HUSH_TURN_MODE_CHILD && mode != HUSH_TURN_MODE_DAEMON)
        return HUSH_ERR_ARG;
    if (turn->password[0] == '\0') {
        if (hush_turn_random_hex(turn->password, 32) != HUSH_OK)
            return HUSH_ERR_IO;
    }
    if (hush_turn_ensure_dir(turn->state_dir) != HUSH_OK)
        return HUSH_ERR_IO;
    if (hush_turn_write_conf(turn) != HUSH_OK)
        return HUSH_ERR_IO;
    if (mode == HUSH_TURN_MODE_DAEMON) {
        if (!turn->have_unit) {
            turn->need_root = 1;
            return HUSH_ERR_DENIED;
        }
        if (hush_turn_exec_systemctl("enable") != HUSH_OK)
            return HUSH_ERR_IO;
        if (hush_turn_exec_systemctl("start") != HUSH_OK)
            return HUSH_ERR_IO;
        turn->mode = HUSH_TURN_MODE_DAEMON;
        turn->enabled = 1;
        turn->running = 1;
        return HUSH_OK;
    }
    if (!turn->have_binary)
        return HUSH_ERR_NOT_FOUND;
    if (hush_turn_spawn_child(turn) != HUSH_OK)
        return HUSH_ERR_IO;
    turn->mode = HUSH_TURN_MODE_CHILD;
    turn->enabled = 1;
    turn->running = 1;
    return HUSH_OK;
}

hush_status_t hush_turn_disable(hush_turn_t *turn)
{
    if (turn == NULL)
        return HUSH_ERR_ARG;
    if (turn->mode == HUSH_TURN_MODE_DAEMON)
        (void)hush_turn_exec_systemctl("stop");
    if (turn->child_pid > 0) {
        hush_turn_kill_pid(turn->child_pid);
        turn->child_pid = 0;
    }
    turn->mode = HUSH_TURN_MODE_OFF;
    turn->enabled = 0;
    turn->running = 0;
    return HUSH_OK;
}

void hush_turn_refresh(hush_turn_t *turn)
{
    if (turn == NULL || !turn->enabled)
        return;
    if (turn->mode == HUSH_TURN_MODE_CHILD && turn->child_pid > 0)
        turn->running = hush_turn_alive(turn->child_pid);
}

void hush_turn_shutdown(hush_turn_t *turn)
{
    if (turn == NULL)
        return;
    if (turn->mode == HUSH_TURN_MODE_CHILD)
        (void)hush_turn_disable(turn);
}

int hush_turn_whisper_available(void)
{
    const char *env;

    env = getenv("HUSH_WHISPER");
    if (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0)
        return 1;
    if (access("/usr/bin/whisper", X_OK) == 0)
        return 1;
    if (access("/usr/local/bin/whisper", X_OK) == 0)
        return 1;
    return 0;
}

hush_status_t hush_turn_format_ice(const hush_turn_t *turn,
                                   char *out, size_t outsz, size_t *out_len)
{
    int n;
    const char *host;
    unsigned port;

    if (turn == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    host = turn->public_host[0] ? turn->public_host : HUSH_TURN_HOST_LOCAL;
    port = (unsigned)turn->listen_port;
    if (!turn->enabled) {
        n = snprintf(out, outsz,
                     "{\"ok\":true,\"compiled\":%s,\"running\":false,"
                     "\"iceServers\":[{\"urls\":"
                     "[\"stun:stun.l.google.com:19302\"]}]}\n",
                     turn->compiled_in ? "true" : "false");
    } else {
        n = snprintf(out, outsz,
                     "{\"ok\":true,\"compiled\":true,\"running\":%s,"
                     "\"iceServers\":["
                     "{\"urls\":[\"stun:%s:%u\"]},"
                     "{\"urls\":[\"turn:%s:%u?transport=udp\","
                     "\"turn:%s:%u?transport=tcp\"],"
                     "\"username\":\"%s\",\"credential\":\"%s\"}]}\n",
                     turn->running ? "true" : "false",
                     host, port, host, port, host, port,
                     turn->username, turn->password);
    }
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    if (out_len != NULL)
        *out_len = (size_t)n;
    return HUSH_OK;
}

hush_status_t hush_turn_format_status(const hush_turn_t *turn,
                                      char *out, size_t outsz, size_t *out_len)
{
    int n;
    const char *mode;

    if (turn == NULL || out == NULL || outsz == 0)
        return HUSH_ERR_ARG;
    if (turn->mode == HUSH_TURN_MODE_DAEMON)
        mode = "daemon";
    else if (turn->mode == HUSH_TURN_MODE_CHILD)
        mode = "child";
    else
        mode = "off";
    n = snprintf(out, outsz,
                 "{\"ok\":true,\"compiled\":%s,\"enabled\":%s,\"running\":%s,"
                 "\"have_binary\":%s,\"have_unit\":%s,\"need_root\":%s,"
                 "\"mode\":\"%s\",\"port\":%u,\"host\":\"%s\","
                 "\"whisper\":%s}\n",
                 turn->compiled_in ? "true" : "false",
                 turn->enabled ? "true" : "false",
                 turn->running ? "true" : "false",
                 turn->have_binary ? "true" : "false",
                 turn->have_unit ? "true" : "false",
                 turn->need_root ? "true" : "false",
                 mode, (unsigned)turn->listen_port, turn->public_host,
                 hush_turn_whisper_available() ? "true" : "false");
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    if (out_len != NULL)
        *out_len = (size_t)n;
    return HUSH_OK;
}

static void hush_turn_copy(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    if (src == NULL)
        src = "";
    while (src[i] != '\0' && i + 1 < dstsz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void hush_turn_resolve_state(hush_turn_t *turn)
{
    const char *env;
    const char *home;

    env = getenv("HUSH_STATE_DIR");
    if (env != NULL && env[0] != '\0') {
        hush_turn_copy(turn->state_dir, sizeof(turn->state_dir), env);
    } else {
        env = getenv("XDG_STATE_HOME");
        home = getenv("HOME");
        if (env != NULL && env[0] != '\0')
            (void)snprintf(turn->state_dir, sizeof(turn->state_dir),
                           "%s/hush", env);
        else if (home != NULL && home[0] != '\0')
            (void)snprintf(turn->state_dir, sizeof(turn->state_dir),
                           "%s/.local/state/hush", home);
        else
            hush_turn_copy(turn->state_dir, sizeof(turn->state_dir),
                           "/tmp/hush");
    }
    if (strlen(turn->state_dir) > 300)
        hush_turn_copy(turn->state_dir, sizeof(turn->state_dir), "/tmp/hush");
    (void)snprintf(turn->conf_path, sizeof(turn->conf_path),
                   "%s/turnserver.conf", turn->state_dir);
}

static void hush_turn_find_binary(hush_turn_t *turn)
{
    const char *env;
    size_t i;

    env = getenv("TURNSERVER");
    if (env != NULL && env[0] != '\0' && access(env, X_OK) == 0) {
        hush_turn_copy(turn->binary, sizeof(turn->binary), env);
        turn->have_binary = 1;
        return;
    }
    for (i = 0; hush_turn_bin_paths[i] != NULL; ++i) {
        if (access(hush_turn_bin_paths[i], X_OK) == 0) {
            hush_turn_copy(turn->binary, sizeof(turn->binary),
                           hush_turn_bin_paths[i]);
            turn->have_binary = 1;
            return;
        }
    }
}

static int hush_turn_unit_present(void)
{
    size_t i;

    for (i = 0; hush_turn_unit_paths[i] != NULL; ++i) {
        if (access(hush_turn_unit_paths[i], F_OK) == 0)
            return 1;
    }
    return 0;
}

static hush_status_t hush_turn_random_hex(char *out, size_t hex_chars)
{
    unsigned char raw[HUSH_TURN_SECRET_BYTES];
    int fd;
    ssize_t n;
    size_t i;

    assert(out != NULL);
    if (hex_chars == 0 || hex_chars > 32)
        return HUSH_ERR_ARG;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return HUSH_ERR_IO;
    n = read(fd, raw, HUSH_TURN_SECRET_BYTES);
    close(fd);
    if (n != (ssize_t)HUSH_TURN_SECRET_BYTES)
        return HUSH_ERR_IO;
    for (i = 0; i < hex_chars; ++i)
        out[i] = HUSH_TURN_HEX[raw[i / 2] >> ((i % 2) ? 0 : 4) & 0x0f];
    out[hex_chars] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_turn_ensure_dir(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return HUSH_ERR_ARG;
    if (mkdir(path, 0700) == 0)
        return HUSH_OK;
    if (errno == EEXIST)
        return HUSH_OK;
    return HUSH_ERR_IO;
}

static hush_status_t hush_turn_write_conf(const hush_turn_t *turn)
{
    FILE *fp;
    char log_path[HUSH_TURN_PATH_MAX];
    char pid_path[HUSH_TURN_PATH_MAX];

    assert(turn != NULL);
    if (snprintf(log_path, sizeof(log_path), "%s/turnserver.log",
                 turn->state_dir) >= (int)sizeof(log_path))
        return HUSH_ERR_ARG;
    if (snprintf(pid_path, sizeof(pid_path), "%s/turnserver.pid",
                 turn->state_dir) >= (int)sizeof(pid_path))
        return HUSH_ERR_ARG;
    fp = fopen(turn->conf_path, "w");
    if (fp == NULL)
        return HUSH_ERR_IO;
    (void)fprintf(fp,
                  "listening-port=%u\nmin-port=%u\nmax-port=%u\n"
                  "fingerprint\nlt-cred-mech\nrealm=%s\nuser=%s:%s\n"
                  "no-cli\nno-tls\nno-dtls\nno-multicast-peers\n"
                  "stale-nonce=600\nlog-file=%s\npidfile=%s\n",
                  (unsigned)turn->listen_port,
                  (unsigned)HUSH_TURN_RELAY_MIN,
                  (unsigned)HUSH_TURN_RELAY_MAX,
                  turn->realm, turn->username, turn->password,
                  log_path, pid_path);
    if (strcmp(turn->public_host, HUSH_TURN_HOST_LOCAL) != 0)
        (void)fprintf(fp, "external-ip=%s\n", turn->public_host);
    if (fclose(fp) != 0)
        return HUSH_ERR_IO;
    return HUSH_OK;
}

static hush_status_t hush_turn_spawn_child(hush_turn_t *turn)
{
    pid_t pid;

    assert(turn != NULL);
    if (turn->child_pid > 0)
        hush_turn_kill_pid(turn->child_pid);
    pid = fork();
    if (pid < 0)
        return HUSH_ERR_IO;
    if (pid == 0) {
        execl(turn->binary, "turnserver", "-c", turn->conf_path,
              "--no-cli", (char *)NULL);
        _exit(127);
    }
    turn->child_pid = pid;
    return HUSH_OK;
}

static void hush_turn_kill_pid(pid_t pid)
{
    if (pid > 0)
        (void)kill(pid, SIGTERM);
}

static int hush_turn_alive(pid_t pid)
{
    if (pid <= 0)
        return 0;
    if (kill(pid, 0) == 0)
        return 1;
    return errno == EPERM;
}

static hush_status_t hush_turn_exec_systemctl(const char *verb)
{
    pid_t pid;

    assert(verb != NULL);
    pid = fork();
    if (pid < 0)
        return HUSH_ERR_IO;
    if (pid == 0) {
        execlp("systemctl", "systemctl", verb, "hush-turn.service",
               (char *)NULL);
        _exit(127);
    }
    return HUSH_OK;
}
