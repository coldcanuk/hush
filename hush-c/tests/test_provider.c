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

static void expect_overlay_clean(const char *home)
{
    char path[320];
    char overlay[HUSH_PROVIDER_JSON_MAX];
    FILE *fp;
    size_t n = 0;

    snprintf(path, sizeof(path), "%s/.hush/config/providers.json", home);
    overlay[0] = '\0';
    fp = fopen(path, "r");
    if (fp != NULL) {
        n = fread(overlay, 1, sizeof(overlay) - 1, fp);
        overlay[n] = '\0';
        fclose(fp);
    }
    expect(strstr(overlay, "sk-test-key") == NULL, "overlay no key");
    expect(strstr(overlay, "user-alice") == NULL, "overlay no user");
    expect(strstr(overlay, "pw-secret") == NULL, "overlay no password");
    expect(strstr(overlay, "tok-secret") == NULL, "overlay no token");
    expect(strstr(overlay, "pk-secret") == NULL, "overlay no passkey");
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
    expect(hush_provider_is_id("deepseek-api"), "deepseek id");
    expect(hush_provider_is_id("agy"), "agy id");
    expect(hush_provider_is_id("copilot"), "copilot id");
    expect(hush_provider_is_id("ollama"), "ollama id");
    expect(hush_provider_is_id("custom"), "custom id");
    hush_provider_default_host(host, sizeof(host), "openai-api");
    expect(strcmp(host, HUSH_PROVIDER_HOST_OPENAI) == 0, "openai host");
    hush_provider_default_host(host, sizeof(host), "deepseek-api");
    expect(strcmp(host, HUSH_PROVIDER_HOST_DEEPSEEK) == 0, "deepseek host");
    hush_provider_family(family, sizeof(family), "goose");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_HOME) == 0, "goose family");
    hush_provider_family(family, sizeof(family), "gemini-api");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_API) == 0, "gemini family");
    hush_provider_family(family, sizeof(family), "deepseek-api");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_API) == 0, "deepseek family");
    hush_provider_family(family, sizeof(family), "cline");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_EDITOR) == 0, "cline family");
    hush_provider_family(family, sizeof(family), "agy");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_HOME) == 0, "agy family");
    hush_provider_family(family, sizeof(family), "copilot");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_HOME) == 0, "copilot family");
    hush_provider_family(family, sizeof(family), "ollama");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_LOCAL) == 0, "ollama family");
    hush_provider_family(family, sizeof(family), "custom");
    expect(strcmp(family, HUSH_PROVIDER_FAMILY_API) == 0, "custom family");
    hush_provider_default_host(host, sizeof(host), "ollama");
    expect(strcmp(host, HUSH_PROVIDER_HOST_OLLAMA) == 0, "ollama host");

    expect(hush_provider_capabilities("grok-build") ==
               (HUSH_PROVIDER_CAP_TOOLS | HUSH_PROVIDER_CAP_IMAGE |
                HUSH_PROVIDER_CAP_FILE_ATTACH),
           "grok caps full");
    expect(hush_provider_can("grok-build", HUSH_PROVIDER_CAP_TOOLS),
           "grok can tools");
    expect(hush_provider_can("grok-build", HUSH_PROVIDER_CAP_IMAGE),
           "grok can image");
    expect(hush_provider_can("grok-build", HUSH_PROVIDER_CAP_FILE_ATTACH),
           "grok can file");
    expect(hush_provider_capabilities("goose") ==
               (HUSH_PROVIDER_CAP_TOOLS | HUSH_PROVIDER_CAP_FILE_ATTACH),
           "goose caps tools+file");
    expect(!hush_provider_can("goose", HUSH_PROVIDER_CAP_IMAGE),
           "goose no image");
    expect(hush_provider_capabilities("openai-api") ==
               HUSH_PROVIDER_CAP_IMAGE,
           "openai image only");
    expect(!hush_provider_can("openai-api", HUSH_PROVIDER_CAP_TOOLS),
           "openai no tools");
    expect(hush_provider_capabilities("deepseek-api") == 0,
           "deepseek caps none");
    expect(!hush_provider_can("deepseek-api", HUSH_PROVIDER_CAP_IMAGE),
           "deepseek no image");
    expect(hush_provider_capabilities("nope") == 0, "unknown caps zero");
    expect(!hush_provider_can("nope", HUSH_PROVIDER_CAP_TOOLS), "unknown no");
    expect(hush_provider_capabilities("agy") ==
               (HUSH_PROVIDER_CAP_TOOLS | HUSH_PROVIDER_CAP_FILE_ATTACH),
           "agy caps tools+file");
    expect(!hush_provider_can("agy", HUSH_PROVIDER_CAP_IMAGE),
           "agy no image");
    expect(hush_provider_capabilities("copilot") ==
               (HUSH_PROVIDER_CAP_TOOLS | HUSH_PROVIDER_CAP_FILE_ATTACH),
           "copilot caps tools+file");
    expect(hush_provider_capabilities("ollama") == 0, "ollama caps none");
    expect(hush_provider_capabilities("custom") == 0, "custom caps none");
    expect(hush_provider_flags("agy") ==
               (HUSH_PROVIDER_FLAG_SPAWN_ONLY | HUSH_PROVIDER_FLAG_ALLOWLIST),
           "agy spawn-only + allowlist");
    expect(hush_provider_flags("copilot") == HUSH_PROVIDER_FLAG_OAUTH,
           "copilot oauth");
    expect(hush_provider_flags("grok-build") == HUSH_PROVIDER_FLAG_OAUTH,
           "grok oauth flag");
    expect(hush_provider_flags("ollama") == HUSH_PROVIDER_FLAG_NONE,
           "ollama no flags");
    expect(hush_provider_flags("custom") == HUSH_PROVIDER_FLAG_NONE,
           "custom no flags");
    expect(hush_provider_flags("nope") == 0, "unknown flags zero");

    expect(hush_provider_status(&st, "goose") == HUSH_OK, "goose status");
    expect(st.caps ==
               (HUSH_PROVIDER_CAP_TOOLS | HUSH_PROVIDER_CAP_FILE_ATTACH),
           "goose status caps");
    expect(!st.has_home, "goose home absent");
    expect(hush_provider_status(&st, "nope") == HUSH_ERR_PARSE, "bad id");

    snprintf(path, sizeof(path), "%s/.config/goose/config.yaml", home);
    write_file(path, "active_provider: xai_oauth\nproviders:\n  xai_oauth:\n"
                    "    model: grok-4.6\n    configured: true\n");
    expect(hush_provider_status(&st, "goose") == HUSH_OK, "goose after yaml");
    expect(st.has_home, "goose home present");
    expect(strcmp(st.home_model, "grok-4.6") == 0, "goose model");

    expect(hush_provider_status(&st, "grok-build") == HUSH_OK, "grok status");
    expect(!st.has_home, "grok home absent");
    expect(hush_provider_status(&st, "codex") == HUSH_OK, "codex status");
    expect(!st.has_home, "codex home absent");
    snprintf(path, sizeof(path), "%s/.codex", home);
    if (mkdir(path, 0755) != 0)
        return 1;
    expect(hush_provider_status(&st, "codex") == HUSH_OK, "codex dir only");
    expect(!st.has_home, "codex dir is not auth");
    snprintf(path, sizeof(path), "%s/.codex/auth.json", home);
    write_file(path, "{\"token\":\"x\"}\n");
    expect(hush_provider_status(&st, "codex") == HUSH_OK, "codex auth file");
    expect(st.has_home, "codex auth is home");
    snprintf(path, sizeof(path), "%s/.grok", home);
    if (mkdir(path, 0755) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/.grok/auth.json", home);
    write_file(path, "{\"token\":\"g\"}\n");
    expect(hush_provider_status(&st, "grok-build") == HUSH_OK, "grok auth file");
    expect(st.has_home, "grok auth is home");
    expect(hush_provider_status(&st, "codex") == HUSH_OK, "codex still own file");
    expect(st.has_home, "codex not flipped by grok");

    /* Copilot OAuth: detected via ~/.copilot/config.json loggedInUsers. */
    expect(hush_provider_status(&st, "copilot") == HUSH_OK, "copilot status");
    expect(!st.has_home, "copilot absent before config");
    snprintf(path, sizeof(path), "%s/.copilot", home);
    if (mkdir(path, 0755) != 0)
        return 1;
    snprintf(path, sizeof(path), "%s/.copilot/config.json", home);
    write_file(path, "{\"loggedInUsers\":[{\"login\":\"coldcanuk\"}]}\n");
    expect(hush_provider_status(&st, "copilot") == HUSH_OK,
           "copilot after config");
    expect(st.has_home, "copilot config is home");

    memset(&in, 0, sizeof(in));
    memcpy(in.id, "openai-api", 11);
    memcpy(in.host, "https://api.openai.com", 23);
    memcpy(in.model, "gpt-4o", 7);
    in.api_key = "sk-test-key";
    in.username = "user-alice";
    in.password = "pw-secret";
    in.token = "tok-secret";
    in.passkey = "pk-secret";
    expect(hush_provider_save(&in) == HUSH_OK, "save openai");
    expect(hush_provider_status(&st, "openai-api") == HUSH_OK, "openai status");
    expect(st.has_key, "has key");
    expect(st.has_username, "has username");
    expect(st.has_password, "has password");
    expect(st.has_token, "has token");
    expect(st.has_passkey, "has passkey");
    expect(strcmp(st.model, "gpt-4o") == 0, "model saved");
    expect(st.configured, "configured");
    expect(hush_pass_has("providers/openai-api/api_key"), "pass path");
    expect(hush_pass_has("providers/openai-api/username"), "user path");
    expect(hush_pass_has("providers/openai-api/password"), "pass path word");
    expect(hush_pass_has("providers/openai-api/token"), "token path");
    expect(hush_pass_has("providers/openai-api/passkey"), "passkey path");
    hush_provider_secret_path(path, sizeof(path), "openai-api",
                              HUSH_PROVIDER_SECRET_USERNAME);
    expect(strcmp(path, "providers/openai-api/username") == 0, "secret path");
    expect_overlay_clean(home);

    expect(hush_provider_status_all(all, &n) == HUSH_OK, "all");
    expect(n == (size_t)HUSH_PROVIDER_COUNT, "all providers");

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
    expect(hush_provider_scan(&scan, "openai-api",
                              "https://api.openai.com", NULL) == HUSH_OK,
           "scan from pass");
    expect(scan.nmodels == 2, "two models from pass");
    expect(strcmp(scan.models[0], "gpt-4o") == 0, "first model");
    expect(strcmp(scan.models[1], "o3") == 0, "second model");

    write_file(path, "#!/bin/sh\nexit 1\n");
    expect(hush_provider_scan(&scan, "openai-api",
                              "https://api.openai.com", "sk-test")
               == HUSH_ERR_IO,
           "scan fail");
    expect(scan.error[0] != '\0', "scan error text");

    {
        char err[HUSH_PROVIDER_ERR_MAX];
        char saved_path[1024];

        if (setenv("HUSH_PROVIDER_TERM", "/bin/true", 1) != 0)
            return 1;
        snprintf(path, sizeof(path), "%s/grok", bindir);
        write_file(path, "#!/bin/sh\nexit 0\n");
        if (chmod(path, 0755) != 0)
            return 1;
        expect(hush_provider_start_login("grok-build") == HUSH_OK,
               "grok login spawn");
        hush_provider_last_error(err, sizeof(err));
        expect(err[0] == '\0', "grok login no error");
        snprintf(path, sizeof(path), "%s/copilot", bindir);
        write_file(path, "#!/bin/sh\nexit 0\n");
        if (chmod(path, 0755) != 0)
            return 1;
        expect(hush_provider_start_login("copilot") == HUSH_OK,
               "copilot login spawn");
        hush_provider_last_error(err, sizeof(err));
        expect(err[0] == '\0', "copilot login no error");
        expect(hush_provider_start_login("goose") == HUSH_ERR_IO,
               "goose login refused");
        hush_provider_last_error(err, sizeof(err));
        expect(strcmp(err, "login not offered") == 0, "goose login msg");
        expect(hush_provider_start_login("nope") == HUSH_ERR_IO,
               "unknown login refused");
        hush_provider_last_error(err, sizeof(err));
        expect(strcmp(err, "unknown provider") == 0, "unknown login msg");
        snprintf(saved_path, sizeof(saved_path), "%s",
                 getenv("PATH") != NULL ? getenv("PATH") : "");
        if (setenv("PATH", "/tmp/hush-empty-path", 1) != 0)
            return 1;
        expect(hush_provider_start_login("grok-build") == HUSH_ERR_IO,
               "grok missing binary");
        hush_provider_last_error(err, sizeof(err));
        expect(strcmp(err, "binary missing") == 0, "missing binary msg");
        if (setenv("PATH", saved_path, 1) != 0)
            return 1;
    }

    {
        char saved_path[1024];

        snprintf(path, sizeof(path), "%s/codex", bindir);
        write_file(path, "#!/bin/sh\nexit 0\n");
        if (chmod(path, 0755) != 0)
            return 1;
        /* Isolate PATH to the fake bin dir so real host binaries (goose, agy,
         * …) never leak into the update scan and spawn real updates during
         * the test. bindir already holds fake grok, copilot, and codex. */
        snprintf(newpath, sizeof(newpath), "%s", bindir);
        if (setenv("PATH", newpath, 1) != 0)
            return 1;
        expect(hush_provider_update_all() == 3, "update all spawns grok+copilot+codex");
        snprintf(saved_path, sizeof(saved_path), "%s",
                 getenv("PATH") != NULL ? getenv("PATH") : "");
        if (setenv("PATH", "/tmp/hush-empty-path", 1) != 0)
            return 1;
        expect(hush_provider_update_all() == 0, "update all empty path zero");
        if (setenv("PATH", saved_path, 1) != 0)
            return 1;
    }

    hush_pass_set_helper(NULL);
    if (g_fail)
        return 1;
    printf("ok\n");
    return 0;
}
