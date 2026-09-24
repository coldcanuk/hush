# Hush screenshots

Every image below is a **real capture from a running relay on a cloud VM** — base `44c66f88`, throwaway `HUSH_HOME`/`HUSH_CONFIG_DIR`, `hush-c/hush-relay --no-open 18084`, headless Chrome via `puppeteer-core` (system Chrome, `--no-sandbox`). Desktop shots are 1280×800; phone shots are 390×844 (`isMobile`, touch). The hive was onboarded through the genuine wizard (Begin → create identity → ack backup → stand up hive → Carry on); the thread view comes from a real `@Major` mention whose provider is not installed, so the relay posts its honest "no selected provider" note. Nothing is mocked, edited, or cropped beyond element framing.

## Field-office three-pane

The dispatch log in the center, Active personnel + Status feed on the right, quick-bar folder tabs along the bottom, VOLUME/SCROLL dial above SEND DISPATCH. Desktop shows all three panes; phone collapses to a single column with thin Inventory/Character tabs and the roster as a strip.

![Hush field-office three-pane on desktop: Official Dispatch Log with Major's intro note, Active personnel roster, Status feed showing relay live on port 18084, volume dial, Send Dispatch composer, and the Inventory/Character/New channel/Stop quick bar](docs/assets/screenshots/field-office-desktop.png)

![Hush field-office layout on a 390-pixel phone viewport: single column with Inventory/Character thin tabs, dispatch log, composer, roster strip, status feed, and folder-tab quick bar](docs/assets/screenshots/field-office-phone.png)

## Inventory / Armory (ALWAYS-ON / ON-CALL shelves)

Opened with the `i` key on the Coach template's editor, scrolled to `#skill-armory`. The shelves read exactly `ALWAYS-ON · browser-only` and `ON-CALL · browser-only` with scope facet tabs (All / System / This robot) above — matching the UI_SPEC honesty note: lifetime is a client-side product label; the relay injects every equipped skill body on every job. The "No On-call gems in this view" line is real: system skills default to ALWAYS-ON.

![Armory lifetime shelves on desktop: ALWAYS-ON browser-only shelf of skill gems with Always chips, empty ON-CALL shelf, and All/System/This-robot scope facets](docs/assets/screenshots/armory-desktop.png)

![Armory lifetime shelves on phone: same ALWAYS-ON and ON-CALL browser-only shelves and scope facets in the narrow viewport](docs/assets/screenshots/armory-phone.png)

## Character (`c`)

The `c` key toggles the Profile character sheet: identity, npub, name/org fields, avatar upload, and a **read-only** equipped strip (`Coach · 1/8 saved on this relay`, Always-on / On-call breakdown flagged browser-only) — no Armory forge here, per spec.

![Character sheet on desktop: Profile with npub, name fields, avatar picker, read-only equipped-skills strip, Copy npub, Logout, Close](docs/assets/screenshots/character-desktop.png)

![Character sheet on phone: same Profile drawer with equipped-skills strip in the narrow viewport](docs/assets/screenshots/character-phone.png)

## Quick bar 1–4

Thin manila folder-tab strip (`#quick-bar`), never fat pills: `INVENTORY 1`, `CHARACTER 2`, `NEW CHANNEL 3`, `STOP 4`, plus the `⋯` gear that opens the slot picker. Keys 1–4 fire the slots; typing in inputs never triggers them.

![Quick bar folder tabs on desktop: Inventory 1, Character 2, New channel 3, Stop 4, and the gear picker](docs/assets/screenshots/quickbar-desktop.png)

![Quick bar folder tabs on phone: same four slots and gear, ellipsized to fit 390 pixels](docs/assets/screenshots/quickbar-phone.png)

## Volume dial

UI-M11 stereo-style dial (`#fo-dial`) in the empty chrome above Send Dispatch. Wheel over the dial, clockwise/counter-clockwise drag, or arrow/PageUp/PageDown/Home/End keys scroll the dispatch log; the native fat scrollbar stays hidden and the message column owns no in-column chrome.

![Volume dial on desktop: stamped VOLUME/SCROLL plate with ink knob above Send Dispatch](docs/assets/screenshots/volume-dial-desktop.png)

![Volume dial on phone: same VOLUME/SCROLL dial plate above the composer](docs/assets/screenshots/volume-dial-phone.png)

## Thread view

`Thread · Major`, opened from the channel root's Thread button after a genuine `@Major` mention. Visible: the human root with mention pill, Major's one-time on-deck intro, and the relay's honest leash note ("No selected provider is ready for Major…") — no AI binary is installed on the capture VM, so this is exactly what thread memory looks like without a live provider. The thread composer and SEND button sit below.

![Thread view on desktop: Thread Major pane with the mentioning root, on-deck intro, honest no-provider leash note, and thread composer](docs/assets/screenshots/thread-desktop.png)

![Thread view on phone: same Thread Major pane with root, intro, leash note, and composer in the narrow viewport](docs/assets/screenshots/thread-phone.png)

## What is not shown

Every screen requested for this page was reachable on `main` with no AI provider installed, so nothing had to be omitted. Favorite skill loadouts (PE-4 roadmap) have no UI on `main` and therefore no screenshot here.
