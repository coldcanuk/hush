/* Icon atlas catalog for hush inventory prototypes. */
#ifndef ICONS_H
#define ICONS_H

#include <stddef.h>

enum {
        ICON_TILE_PX = 128,
        ICON_ATLAS_COLS = 8,
        ICON_ATLAS_ROWS = 8,
        ICON_INNER_PAD_PX = 8,
        ICON_COUNT_PER_SHEET = 64,
        ICON_SHEET_COUNT = 6,
        ICON_TOTAL = 384
};

enum IconSheet {
        ICON_SHEET_DOGS = 0,
        ICON_SHEET_CATS = 1,
        ICON_SHEET_SHEEP = 2,
        ICON_SHEET_VIRUS = 3,
        ICON_SHEET_ROBOTS = 4,
        ICON_SHEET_ANGEVIN = 5
};

enum IconClass {
        ICON_CLASS_MECH = 0,
        ICON_CLASS_WIZARD = 1,
        ICON_CLASS_STEAMPUNK = 2,
        ICON_CLASS_KNIGHT = 3,
        ICON_CLASS_WW2_PILOT = 4,
        ICON_CLASS_NINJA = 5,
        ICON_CLASS_CYBER = 6,
        ICON_CLASS_ROYAL = 7,
        ICON_CLASS_COUNT = 8
};

enum IconRank {
        ICON_RANK_STANDARD = 0,
        ICON_RANK_HEAVY = 1,
        ICON_RANK_SCOUT = 2,
        ICON_RANK_AERIAL = 3,
        ICON_RANK_MEDIC = 4,
        ICON_RANK_COMMANDER = 5,
        ICON_RANK_VETERAN = 6,
        ICON_RANK_LEGENDARY = 7,
        ICON_RANK_COUNT = 8
};

enum IconSpecies {
        ICON_SPECIES_DOG = 0,
        ICON_SPECIES_CAT = 1,
        ICON_SPECIES_SHEEP = 2,
        ICON_SPECIES_COUNT = 3
};

struct IconSrc {
        int sx;
        int sy;
        int sw;
        int sh;
};

struct IconRec {
        const char *id;
        const char *name;
        int sheet;
        int index;
        int col;
        int row;
        const char *role;
        const char *rank;
        const char *species;
};

/* Returns the workstation path for sheet, or NULL if sheet is out of range. */
const char *icon_sheet_path(int sheet);

/* Returns the filename only for sheet, or NULL if sheet is out of range. */
const char *icon_sheet_filename(int sheet);

/* Source rectangle inside any 8x8 atlas. index is 0..63. */
struct IconSrc icon_src(int index);

/* Dogs/cats/sheep share this layout: index = rank * 8 + class. */
int icon_animal_index(int rank, int class_id);

/* Linear search of the 384-record table. Returns NULL if id is unknown. */
const struct IconRec *icon_find(const char *id);

extern const struct IconRec icon_table[ICON_TOTAL];

#endif
