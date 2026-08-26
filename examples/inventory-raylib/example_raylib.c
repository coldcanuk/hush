/* Optional Raylib viewer for the six hush icon sheets.
 * Build only if raylib is installed:
 *   gcc example_raylib.c icons.c -o icon_view `pkg-config --cflags --libs raylib`
 * Do not add raylib to hush-c/Makefile.
 */

#include "icons.h"

#include "raylib.h"

int
main(void)
{
        Texture2D sheets[ICON_SHEET_COUNT];
        int sheet;
        int i;
        const struct IconRec *pilot;
        struct IconSrc src;

        InitWindow(1024, 1024, "hush icon atlas");
        SetTargetFPS(60);

        for (sheet = 0; sheet < ICON_SHEET_COUNT; sheet++) {
                sheets[sheet] = LoadTexture(icon_sheet_path(sheet));
        }

        sheet = ICON_SHEET_DOGS;
        while (!WindowShouldClose()) {
                if (IsKeyPressed(KEY_RIGHT)) {
                        sheet = (sheet + 1) % ICON_SHEET_COUNT;
                }
                if (IsKeyPressed(KEY_LEFT)) {
                        sheet = (sheet + ICON_SHEET_COUNT - 1) % ICON_SHEET_COUNT;
                }

                BeginDrawing();
                ClearBackground((Color){22, 22, 22, 255});
                DrawTexture(sheets[sheet], 0, 0, WHITE);

                pilot = icon_find("dog.standard.ww2_pilot");
                if (pilot != NULL && sheet == pilot->sheet) {
                        src = icon_src(pilot->index);
                        DrawRectangleLines(src.sx, src.sy, src.sw, src.sh, GREEN);
                }

                DrawText(icon_sheet_filename(sheet), 12, 12, 20, RAYWHITE);
                EndDrawing();
        }

        for (i = 0; i < ICON_SHEET_COUNT; i++) {
                UnloadTexture(sheets[i]);
        }
        CloseWindow();
        return 0;
}
