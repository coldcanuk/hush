/* tests/test_provider.c: detect, overlay, pass key, fake-curl scan. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_pass.h"
#include "hush_provider.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void write_file(const char *path, const char *body)
{
    FILE *fp = fopen(path, "w");

    if (fp == NULL)
        return;
    fputs(body, fp);
    fclose(fp);
}

static int setup_home(char *home, size_t homesz)
{
    char cmd[512];

    snprintf(home, homesz, "/tmp/hush-provider-test-%d", (int)getpid());
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s/.config/goose %s/bin",
             home, home, home);
    if (system(cmd) != 0)
        return 0;
    if (setenv("HOME", home, 1) != 0)
        return 0;
    unsetenv("XDG_CONFIG_HOME");
    return 1;
}

int main(void)
{
    char home[128];
    char path[320];
    char bindir[192];
    char newpath[1024];
    hush_provider_status_t st;
    hush_provider_status_t all[HUSH_PROVIDER_COUNT];
    hush_provider_in_t in;
    hush_provider_scan_t scan;
    size_t n = 0;
    char host[HUSH_PROVIDER_HOST_MAX];
    char family[HUSH_PROVIDER_FAMILY_MAX];

    if (!setup_home(home, sizeof(home)))
        return 1;
    snprintf(path, sizeof(path), "%s/.config/hush-pass", home);
    if (setenv("HUSH_FAKE_PASS_DIR", path, 1) != 0)
        return 1;
    hush_pass_set_helper("tests/fake-pass.sh");

    expect(hush_provider_is_id("goose"), "goose id");
    expect(hush_provider_is_id("openai-api"), "openai id");
    expect(!hush_provider_is_id("deepseek-api"), "deepseek not yet");
    hush_provider_default_host(host, sizeof(host), "openai-api");
    expect(strcmp(host, HUSH_PROVIDER_HOST_OPENAI) == 0, "openai host");
    hush_provider_family(family, sizeof(family), "goose");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_HOME) == 0, "goose family");
    hush_provider_family(family, sizeof(family), "gemini-api");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_API) == 0, "gemini family");
    hush_provider_family(family, sizeof(family), "cline");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_EDITOR) == 0, "cline family");

    expect(hush_provider_status(&st, "goose") == HUSH_OK, "goose status");
    expect(!st.has_home, "goose home absent");
    expect(hush_provider_status(&st, "nope") == HUSH_ERR_PARSE, "bad id");

    snprintf(path, sizeof(path), "%s/.config/goose/config.yaml", home);
    write_file(path, "active_provider: xai_oauth\nproviders:\n  xai_oauth:\n"
                    "    model: grok-4.6\n    configured: true\n");
    expect(hush_provider_status(&st, "goose") == HUSH_OK, "goose after yaml");
    expect(st.has_home, "goose home present");
    expect(strcmp(st.home_model, "grok-4.6") == 0, "goose model");

    memset(&in, 0, sizeof(in));
    memcpy(in.id, "openai-api", 11);
    memcpy(in.host, "https://api.openai.com", 23);
    memcpy(in.model, "gpt-4o", 7);
    in.api_key = "sk-test-key";
    expect(hush_provider_save(&in) == HUSH_OK, "save openai");
    expect(hush_provider_status(&st, "openai-api") == HUSH_OK, "openai status");
    expect(st.has_key, "has key");
    expect(strcmp(st.model, "gpt-4o") == 0, "model saved");
    expect(st.configured, "configured");
    expect(hush_pass_has("providers/openai-api/api_key"), "pass path");

    expect(hush_provider_status_all(all, &n) == HUSH_OK, "all");
    expect(n == (size_t)HUSH_PROVIDER_COUNT, "eight");

    snprintf(bindir, sizeof(bindir), "%s/bin", home);
    snprintf(path, sizeof(path), "%s/curl", bindir);
    write_file(path,
               "#!/bin/sh\n"
               "echo '{\"data\":[{\"id\":\"gpt-4o\"},{\"id\":\"o3\"}]}'\n");
    if (chmod(path, 0755) != 0)
        return 1;
    snprintf(newpath, sizeof(newpath), "%s:%s", bindir,
             getenv("PATH") != NULL ? getenv("PATH") : "");
    if (setenv("PATH", newpath, 1) != 0)
        return 1;
    expect(hush_provider_scan(&scan, "openai-api",
                              "https://api.openai.com", "sk-test") == HUSH_OK,
           "scan ok");
    expect(scan.nmodels == 2, "two models");
    expect(strcmp(scan.models[0], "gpt-4o") == 0, "first model");
    expect(strcmp(scan.models[1], "o3") == 0, "second model");

    write_file(path, "#!/bin/sh\nexit 1\n");
    expect(hush_provider_scan(&scan, "openai-api",
                              "https://api.openai.com", "sk-test")
               == HUSH_ERR_IO,
           "scan fail");
    expect(scan.error[0] != '\0', "scan error text");

    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("ok\n");
    return 0;
}
