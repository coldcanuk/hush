/* hush_thread.c: owns durable thread transcripts and rolling briefs. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_dir.h"
#include "hush_event.h"
#include "hush_home.h"
#include "hush_json.h"
#include "hush_thread.h"

#define HUSH_THREAD_DIR "threads"
#define HUSH_THREAD_LOG_SUFFIX ".log"
#define HUSH_THREAD_BRIEF_SUFFIX ".brief"

enum {
    /* Escaped content scratch plus JSON framing. */
    HUSH_THREAD_LINE_MAX = HUSH_THREAD_CONTENT_MAX * HUSH_JSON_U_LEN + 512,
    HUSH_THREAD_FILE_MODE = 0600,
    HUSH_THREAD_NUMBER_MAX = 32
};

/* True when root is a 64-character lowercase hex event id. */
static int hush_thread_root_is_valid(const char *root);

/* Copies the event's thread root (first e tag, else its own id). */
static void hush_thread_root_of(const hush_event_t *ev,
                                char out[HUSH_EVENT_ID_HEX_LEN + 1]);

/* Writes $HUSH_HOME/threads into out. Empty on overflow or a missing home. */
static void hush_thread_dir(char *out, size_t outsz);

/* Writes one of the thread's file paths. DENIED on an invalid root. */
static hush_status_t hush_thread_file(const char *root, const char *suffix,
                                      char *out, size_t outsz);

/* Creates $HUSH_HOME/threads when missing. */
static hush_status_t hush_thread_ensure_dir(void);

/* Parses one record line into out. Returns 0 when the line is not a turn. */
static int hush_thread_parse_line(const char *line, hush_thread_turn_t *out);

/* Opens a thread file read-only without following a symlink. */
static FILE *hush_thread_open_read(const char *path);

/* Copies a bounded file into out, trimming a trailing newline. */
static void hush_thread_read_file(const char *path, char *out, size_t outsz);

/* Writes text to path through a 0600 temp file and rename. */
static hush_status_t hush_thread_write_file(const char *path, const char *text);

void hush_thread_record(const hush_event_t *ev)
{
    char log_path[HUSH_HOME_PATH_MAX];
    char root[HUSH_EVENT_ID_HEX_LEN + 1];
    char capped[HUSH_THREAD_CONTENT_MAX + 1];
    char escaped[HUSH_THREAD_CONTENT_MAX * HUSH_JSON_U_LEN + 1];
    char line[HUSH_THREAD_LINE_MAX];
    size_t len;
    int fd;
    FILE *fp;
    int n;

    if (ev == NULL || ev->kind != 1 || ev->id[0] == '\0')
        return;
    hush_thread_root_of(ev, root);
    if (!hush_thread_root_is_valid(root))
        return;
    if (hush_thread_file(root, HUSH_THREAD_LOG_SUFFIX, log_path,
                         sizeof(log_path)) != HUSH_OK)
        return;
    if (hush_thread_ensure_dir() != HUSH_OK)
        return;
    len = strlen(ev->content);
    if (len > (size_t)HUSH_THREAD_CONTENT_MAX)
        len = (size_t)HUSH_THREAD_CONTENT_MAX;
    memcpy(capped, ev->content, len);
    capped[len] = '\0';
    if (hush_json_escape(capped, escaped, sizeof(escaped)) == 0 && len > 0)
        return;
    n = snprintf(line, sizeof(line),
                 "{\"id\":\"%s\",\"pubkey\":\"%s\",\"at\":%lld,"
                 "\"content\":\"%s\"}\n",
                 ev->id, ev->pubkey, (long long)ev->created_at, escaped);
    if (n <= 0 || (size_t)n >= sizeof(line))
        return;
    fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW,
              HUSH_THREAD_FILE_MODE);
    if (fd < 0)
        return;
    fp = fdopen(fd, "a");
    if (fp == NULL) {
        (void)close(fd);
        return;
    }
    (void)fwrite(line, 1, (size_t)n, fp);
    (void)fclose(fp);
}

size_t hush_thread_read(const char *root, hush_thread_turn_t *out, size_t max)
{
    char log_path[HUSH_HOME_PATH_MAX];
    char line[HUSH_THREAD_LINE_MAX];
    FILE *fp;
    size_t count = 0;

    if (out == NULL || max == 0)
        return 0;
    if (max > (size_t)HUSH_THREAD_TURNS_MAX)
        max = (size_t)HUSH_THREAD_TURNS_MAX;
    if (hush_thread_file(root, HUSH_THREAD_LOG_SUFFIX, log_path,
                         sizeof(log_path)) != HUSH_OK)
        return 0;
    fp = hush_thread_open_read(log_path);
    if (fp == NULL)
        return 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        hush_thread_turn_t turn;

        memset(&turn, 0, sizeof(turn));
        if (!hush_thread_parse_line(line, &turn))
            continue;
        if (count < max) {
            out[count++] = turn;
        } else {
            memmove(out, out + 1, (max - 1) * sizeof(out[0]));
            out[max - 1] = turn;
        }
    }
    (void)fclose(fp);
    return count;
}

size_t hush_thread_count(const char *root)
{
    char log_path[HUSH_HOME_PATH_MAX];
    char line[HUSH_THREAD_LINE_MAX];
    hush_thread_turn_t turn;
    FILE *fp;
    size_t count = 0;

    if (hush_thread_file(root, HUSH_THREAD_LOG_SUFFIX, log_path,
                         sizeof(log_path)) != HUSH_OK)
        return 0;
    fp = hush_thread_open_read(log_path);
    if (fp == NULL)
        return 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (hush_thread_parse_line(line, &turn))
            count++;
    }
    (void)fclose(fp);
    return count;
}

void hush_thread_brief_get(const char *root, char *out, size_t outsz)
{
    char brief_path[HUSH_HOME_PATH_MAX];

    if (out == NULL || outsz == 0)
        return;
    out[0] = '\0';
    if (hush_thread_file(root, HUSH_THREAD_BRIEF_SUFFIX, brief_path,
                         sizeof(brief_path)) != HUSH_OK)
        return;
    hush_thread_read_file(brief_path, out, outsz);
}

void hush_thread_brief_set(const char *root, const char *text)
{
    char brief_path[HUSH_HOME_PATH_MAX];

    if (text == NULL)
        text = "";
    if (hush_thread_file(root, HUSH_THREAD_BRIEF_SUFFIX, brief_path,
                         sizeof(brief_path)) != HUSH_OK)
        return;
    if (text[0] == '\0') {
        (void)unlink(brief_path);
        return;
    }
    if (hush_thread_ensure_dir() != HUSH_OK)
        return;
    (void)hush_thread_write_file(brief_path, text);
}

static void hush_thread_root_of(const hush_event_t *ev,
                                char out[HUSH_EVENT_ID_HEX_LEN + 1])
{
    size_t i;

    assert(ev != NULL);
    assert(out != NULL);
    for (i = 0; i < ev->tag_count && i < (size_t)HUSH_EVENT_MAX_TAGS; ++i) {
        if (strcmp(ev->tags[i][0], "e") == 0 && ev->tags[i][1][0] != '\0') {
            (void)snprintf(out, HUSH_EVENT_ID_HEX_LEN + 1, "%s",
                           ev->tags[i][1]);
            return;
        }
    }
    (void)snprintf(out, HUSH_EVENT_ID_HEX_LEN + 1, "%s", ev->id);
}

static int hush_thread_root_is_valid(const char *root)
{
    size_t i;

    if (root == NULL || strlen(root) != (size_t)HUSH_EVENT_ID_HEX_LEN)
        return 0;
    for (i = 0; i < (size_t)HUSH_EVENT_ID_HEX_LEN; ++i) {
        char ch = root[i];

        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return 0;
    }
    return 1;
}

static void hush_thread_dir(char *out, size_t outsz)
{
    char root[HUSH_HOME_PATH_MAX];
    int n;

    assert(out != NULL);
    out[0] = '\0';
    hush_home_root(root, sizeof(root));
    if (root[0] == '\0')
        return;
    n = snprintf(out, outsz, "%s/%s", root, HUSH_THREAD_DIR);
    if (n <= 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

static hush_status_t hush_thread_file(const char *root, const char *suffix,
                                      char *out, size_t outsz)
{
    char dir[HUSH_HOME_PATH_MAX];
    int n;

    assert(suffix != NULL);
    assert(out != NULL);
    out[0] = '\0';
    if (!hush_thread_root_is_valid(root))
        return HUSH_ERR_DENIED;
    hush_thread_dir(dir, sizeof(dir));
    if (dir[0] == '\0')
        return HUSH_ERR_IO;
    n = snprintf(out, outsz, "%s/%s%s", dir, root, suffix);
    if (n <= 0 || (size_t)n >= outsz) {
        out[0] = '\0';
        return HUSH_ERR_FULL;
    }
    return HUSH_OK;
}

static hush_status_t hush_thread_ensure_dir(void)
{
    char dir[HUSH_HOME_PATH_MAX];

    if (hush_home_ensure() != HUSH_OK)
        return HUSH_ERR_IO;
    hush_thread_dir(dir, sizeof(dir));
    if (dir[0] == '\0')
        return HUSH_ERR_IO;
    return hush_dir_ensure_private(dir);
}

static int hush_thread_parse_line(const char *line, hush_thread_turn_t *out)
{
    hush_json_value_t value = {0};
    char number[HUSH_THREAD_NUMBER_MAX];
    size_t len;

    assert(line != NULL);
    assert(out != NULL);
    memset(out, 0, sizeof(*out));
    if (hush_json_lookup(&value, line, "/id") != HUSH_OK ||
        hush_json_decode(out->id, sizeof(out->id), &value) != HUSH_OK)
        return 0;
    if (hush_json_lookup(&value, line, "/pubkey") != HUSH_OK ||
        hush_json_decode(out->pubkey, sizeof(out->pubkey), &value) != HUSH_OK)
        return 0;
    if (hush_json_lookup(&value, line, "/content") != HUSH_OK ||
        hush_json_decode(out->content, sizeof(out->content), &value) != HUSH_OK)
        return 0;
    if (hush_json_lookup(&value, line, "/at") == HUSH_OK) {
        len = value.len;
        if (len > 0 && len < sizeof(number)) {
            memcpy(number, value.start, len);
            number[len] = '\0';
            out->created_at = (int64_t)strtoll(number, NULL, 10);
        }
    }
    return out->id[0] != '\0' && out->pubkey[0] != '\0';
}

static FILE *hush_thread_open_read(const char *path)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW);

    if (fd < 0)
        return NULL;
    FILE *fp = fdopen(fd, "r");

    if (fp == NULL)
        (void)close(fd);
    return fp;
}

static void hush_thread_read_file(const char *path, char *out, size_t outsz)
{
    FILE *fp;
    size_t n;

    assert(path != NULL);
    assert(out != NULL);
    fp = hush_thread_open_read(path);
    if (fp == NULL)
        return;
    n = fread(out, 1, outsz - 1, fp);
    out[n] = '\0';
    (void)fclose(fp);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
}

static hush_status_t hush_thread_write_file(const char *path, const char *text)
{
    char tmp[HUSH_HOME_PATH_MAX + 8];
    size_t len = strlen(text);
    ssize_t written;
    int n;
    int fd;

    assert(path != NULL);
    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp))
        return HUSH_ERR_FULL;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW,
              HUSH_THREAD_FILE_MODE);
    if (fd < 0)
        return HUSH_ERR_IO;
    written = write(fd, text, len);
    if (written != (ssize_t)len || fsync(fd) != 0) {
        (void)close(fd);
        (void)unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (close(fd) != 0) {
        (void)unlink(tmp);
        return HUSH_ERR_IO;
    }
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return HUSH_ERR_IO;
    }
    return HUSH_OK;
}
