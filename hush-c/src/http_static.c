/* http_static.c: owns PWA asset and icon-panel serving. */

#include <string.h>

#include "hush_http_internal.h"
#include "hush_icon_panels.h"
#include "hush_ui_html.h"

/* Serves an embedded icon panel for path. 0 when path is not ours. */
static int hush_http_serve_icon_panel(int fd, const char *path);

int hush_http_serve_asset(int fd, const char *path)
{
    struct hush_http_asset {
        const char *path;
        const char *ctype;
        const char *body;
        size_t len;
    };
    static const struct hush_http_asset assets[] = {
        { "/", "text/html; charset=utf-8", HUSH_UI_HTML, 0 },
        { "/index.html", "text/html; charset=utf-8", HUSH_UI_HTML, 0 },
        { "/manifest.webmanifest", "application/manifest+json",
          HUSH_UI_MANIFEST, 0 },
        { "/sw.js", "application/javascript; charset=utf-8", HUSH_UI_SW, 0 },
        { "/icon-192.png", "image/png",
          (const char *)HUSH_UI_ICON_192, (size_t)HUSH_UI_ICON_192_LEN },
        { "/icon-512.png", "image/png",
          (const char *)HUSH_UI_ICON_512, (size_t)HUSH_UI_ICON_512_LEN },
        { "/apple-touch-icon.png", "image/png",
          (const char *)HUSH_UI_ICON_180, (size_t)HUSH_UI_ICON_180_LEN },
        { "/agent-atlas.png", "image/png",
          (const char *)HUSH_UI_AGENT_ATLAS, (size_t)HUSH_UI_AGENT_ATLAS_LEN }
    };
    size_t i;
    size_t n;
    const char *body;

    for (i = 0; i < sizeof(assets) / sizeof(assets[0]); ++i) {
        if (strcmp(path, assets[i].path) != 0)
            continue;
        body = assets[i].body;
        n = assets[i].len != 0 ? assets[i].len : strlen(body);
        hush_http_reply(fd, "200 OK", assets[i].ctype, body, n);
        return 1;
    }
    return hush_http_serve_icon_panel(fd, path);
}

static int hush_http_serve_icon_panel(int fd, const char *path)
{
    struct hush_http_panel {
        const char *path;
        const unsigned char *start;
        const unsigned char *end;
    };
    static const struct hush_http_panel panels[] = {
        { "/icons/icon_panel_dogs.png",
          _binary_demo_icons_icon_panel_dogs_png_start,
          _binary_demo_icons_icon_panel_dogs_png_end },
        { "/icons/icon_panel_cats.png",
          _binary_demo_icons_icon_panel_cats_png_start,
          _binary_demo_icons_icon_panel_cats_png_end },
        { "/icons/icon_panel_sheep.png",
          _binary_demo_icons_icon_panel_sheep_png_start,
          _binary_demo_icons_icon_panel_sheep_png_end },
        { "/icons/icon_panel_virus.png",
          _binary_demo_icons_icon_panel_virus_png_start,
          _binary_demo_icons_icon_panel_virus_png_end },
        { "/icons/icon_panel_robots.png",
          _binary_demo_icons_icon_panel_robots_png_start,
          _binary_demo_icons_icon_panel_robots_png_end },
        { "/icons/icon_panel_angevin.png",
          _binary_demo_icons_icon_panel_angevin_png_start,
          _binary_demo_icons_icon_panel_angevin_png_end }
    };
    size_t i;
    size_t n;

    if (path == NULL)
        return 0;
    for (i = 0; i < sizeof(panels) / sizeof(panels[0]); ++i) {
        if (strcmp(path, panels[i].path) != 0)
            continue;
        n = (size_t)(panels[i].end - panels[i].start);
        hush_http_reply(fd, "200 OK", "image/png",
                        (const char *)panels[i].start, n);
        return 1;
    }
    return 0;
}
