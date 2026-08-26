/* Smoke test for icons.c tables. gcc test_icons.c icons.c -o test_icons */

#include "icons.h"

#include <stdio.h>
#include <string.h>

int
main(void)
{
        struct IconSrc src;
        const struct IconRec *rec;
        int idx;
        int fails;

        fails = 0;
        src = icon_src(4);
        if (src.sx != 512 || src.sy != 0 || src.sw != 128 || src.sh != 128) {
                printf("icon_src(4) expected 512,0,128,128 got %d,%d,%d,%d\n",
                       src.sx, src.sy, src.sw, src.sh);
                fails++;
        }

        idx = icon_animal_index(ICON_RANK_STANDARD, ICON_CLASS_WW2_PILOT);
        if (idx != 4) {
                printf("animal index expected 4 got %d\n", idx);
                fails++;
        }

        rec = icon_find("dog.standard.ww2_pilot");
        if (rec == NULL || rec->index != 4 || rec->sheet != ICON_SHEET_DOGS) {
                printf("find dog.standard.ww2_pilot failed\n");
                fails++;
        }

        rec = icon_find("angevin.bishop");
        if (rec == NULL || rec->index != 4) {
                printf("find angevin.bishop failed\n");
                fails++;
        }

        if (icon_sheet_path(ICON_SHEET_VIRUS) == NULL ||
            strstr(icon_sheet_path(ICON_SHEET_VIRUS), "icon_panel_virus.png") == NULL) {
                printf("virus path missing\n");
                fails++;
        }

        if (fails == 0) {
                printf("ok 384 records, src math and find() passed\n");
        }
        return fails == 0 ? 0 : 1;
}
