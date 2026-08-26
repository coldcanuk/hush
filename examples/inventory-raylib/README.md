# inventory-raylib (optional prototype)

Primary robots inventory is the web grid in `hush-c/demo/index.html`.
Do **not** add raylib to `hush-c/Makefile`.

This directory holds the SuperGrok 8×8 sheet catalog and an optional
Raylib viewer:

- `icons.h` / `icons.c` — C11 lookup (`icon_src`, `icon_find`)
- `example_raylib.c` — flip sheets with arrow keys
- `test_icons.c` — table smoke test (no raylib)

Sheets are the hive copies (not duplicated here):

```bash
mkdir -p assets
cp ../../hush-c/demo/icons/icon_panel_*.png assets/

# catalog only
gcc test_icons.c icons.c -o test_icons && ./test_icons

# viewer (needs libraylib)
gcc example_raylib.c icons.c -o icon_view `pkg-config --cflags --libs raylib`
./icon_view
```

Run from this directory so `assets/` paths resolve.

Hive picture ids are `panel:<sheet>:<index>` (index 0..63). Cell math:

```
sx = (index % 8) * 128
sy = (index / 8) * 128
```
