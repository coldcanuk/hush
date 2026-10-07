/* tests/test_ui_face.c: local typewriter face and ink header mark. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hush_http_internal.h"

enum {
    HUSH_FACE_PAGE_MAX = 1 << 20,
    HUSH_FACE_FONT_MAX = 1 << 16
};

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static size_t read_all(int fd, char *buf, size_t cap)
{
    size_t got = 0;
    ssize_t n;

    while (got + 1 < cap &&
           (n = read(fd, buf + got, cap - 1 - got)) > 0)
        got += (size_t)n;
    buf[got] = '\0';
    return got;
}

static int fetch(const char *path, char *buf, size_t cap)
{
    int fds[2];
    pid_t kid;
    int status = 0;

    if (pipe(fds) != 0)
        return 0;
    kid = fork();
    if (kid < 0)
        return 0;
    if (kid == 0) {
        if (close(fds[0]) != 0)
            _exit(1);
        if (hush_http_serve_asset(fds[1], path) != 1)
            _exit(2);
        if (close(fds[1]) != 0)
            _exit(1);
        _exit(0);
    }
    if (close(fds[1]) != 0)
        return 0;
    (void)read_all(fds[0], buf, cap);
    if (close(fds[0]) != 0)
        return 0;
    if (waitpid(kid, &status, 0) != kid)
        return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void test_page(void)
{
    char *buf = malloc(HUSH_FACE_PAGE_MAX);

    expect(buf != NULL, "page buffer");
    if (buf == NULL)
        return;
    expect(fetch("/", buf, HUSH_FACE_PAGE_MAX) == 1, "page route");
    expect(strstr(buf, "/fonts/special-elite-latin-400.woff2") != NULL,
           "page names the local face");
    expect(strstr(buf, "fonts.googleapis.com") == NULL,
           "page drops the Google stylesheet");
    expect(strstr(buf, "064e3b") == NULL, "header mark drops emerald");
    expect(strstr(buf, "34d399") == NULL, "header mark drops feather");
    expect(strstr(buf, "currentColor") != NULL, "mark uses currentColor");
    free(buf);
}

static void test_font_route(void)
{
    char *buf = malloc(HUSH_FACE_FONT_MAX);
    int fd = -1;

    expect(buf != NULL, "font buffer");
    if (buf == NULL)
        return;
    expect(fetch("/fonts/special-elite-latin-400.woff2", buf, HUSH_FACE_FONT_MAX) == 1,
           "font route");
    expect(strstr(buf, "font/woff2") != NULL, "font content type");
    expect(strstr(buf, "wOF2") != NULL, "woff2 body");
    expect(hush_http_serve_asset(fd, "/fonts/missing.woff2") == 0,
           "unknown font misses");
    free(buf);
}

int main(void)
{
    test_page();
    test_font_route();
    if (g_fail)
        return 1;
    printf("test_ui_face ok\n");
    return 0;
}
