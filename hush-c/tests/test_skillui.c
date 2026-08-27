/* tests/test_skillui.c: static HTML/CSS token extract and skill package. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_skillui.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    hush_skillui_t tok;
    char json[HUSH_SKILLUI_JSON_MAX];
    char home[192];
    char dir[HUSH_HOME_PATH_MAX];
    char path[512];
    char body[2048];
    FILE *fp;
    size_t n = 0;
    const char *html =
        "body{color:#112233;background:rgb(1,2,3);"
        "font-family:Inter,sans-serif;font-size:16px;"
        "padding:8px;margin:0 auto;}";

    snprintf(home, sizeof(home), "/tmp/hush-skillui-test-%d", (int)getpid());
    unsetenv("HUSH_CONFIG_DIR");
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    expect(hush_skillui_extract(&tok, html, strlen(html)) == HUSH_OK, "extract");
    expect(tok.ncolors >= 2, "two colors");
    expect(tok.nfonts >= 1, "one font");
    expect(tok.nspaces >= 2, "spacing tokens");
    {
        size_t i;
        int hex = 0;
        int rgb = 0;
        int font = 0;
        int pad = 0;

        for (i = 0; i < tok.ncolors; i++) {
            if (strcmp(tok.colors[i], "#112233") == 0)
                hex = 1;
            if (strcmp(tok.colors[i], "rgb(1,2,3)") == 0)
                rgb = 1;
        }
        for (i = 0; i < tok.nfonts; i++) {
            if (strstr(tok.fonts[i], "Inter") != NULL)
                font = 1;
        }
        for (i = 0; i < tok.nspaces; i++) {
            if (strstr(tok.spaces[i], "8px") != NULL)
                pad = 1;
        }
        expect(hex, "hex color");
        expect(rgb, "rgb color");
        expect(font, "font family");
        expect(pad, "padding");
    }
    expect(hush_skillui_format_json(&tok, json, sizeof(json), &n) == HUSH_OK,
           "json");
    expect(strstr(json, "#112233") != NULL, "hex json");
    expect(strstr(json, "rgb(1,2,3)") != NULL, "rgb json");
    expect(hush_home_ensure() == HUSH_OK, "home");
    expect(hush_home_skills_dir(dir, sizeof(dir), "user", NULL) == HUSH_OK,
           "user dir");
    expect(hush_skillui_write_skill(dir, "Fixture Sheet", &tok) == HUSH_OK,
           "write");
    snprintf(path, sizeof(path), "%s/fixture-sheet/SKILL.md", dir);
    fp = fopen(path, "r");
    expect(fp != NULL, "skill file");
    if (fp != NULL) {
        n = fread(body, 1, sizeof(body) - 1, fp);
        body[n] = '\0';
        fclose(fp);
        expect(strstr(body, "Hush-adapted") != NULL, "adapted package");
        expect(strstr(body, "reverse-engineering") != NULL, "re category");
        expect(strstr(body, "role: worker") != NULL, "worker role");
        expect(strstr(body, "#112233") != NULL, "color in skill");
        expect(strstr(body, "16px") != NULL, "font-size in skill");
    }
    if (g_fail)
        return 1;
    printf("test_skillui ok\n");
    return 0;
}
