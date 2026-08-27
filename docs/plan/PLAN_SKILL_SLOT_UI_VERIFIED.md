# PLAN_SKILL_SLOT_UI — verified

**Branch:** `gb/skill-slot-ui`
**Date:** 2026-08-27

## Commands

```
./configure && make
make test
# two live --no-open relays on 19871 and 19872
curl -sf http://127.0.0.1:PORT/ | grep hive-skill-cycle
curl -sf http://127.0.0.1:PORT/api/skills | grep '"scopes":\["system","user","robot"\]'
```

## Results

- `make test` → ALL TESTS PASSED (including `check_launch.sh` stash/ghost/hold/slot greps).
- Two live launches: hive stash, paper-doll `skill-slot`, `#skill-ghost`, `placeSkillOnRobot`, chip wall absent, catalog scopes present.
- Embed is generated at build (`hush-c/src/hush_ui_html.h` gitignored).

## DoD

Cycle gems in the hive stash. Hold on the cursor. Drop onto a robot tile (persist) or an empty doll socket (Save Robot). Locked templates refuse. Role/watermarks still bind.
