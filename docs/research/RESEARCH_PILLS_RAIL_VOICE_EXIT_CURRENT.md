# RESEARCH: Pills / Rail / Voice / Exit (current base audit)

Base: da6fa9d0b (post robot-cards PR #57, post server-ack-notes M5)

## Audit vs PLAN_PILLS_RAIL_VOICE_EXIT.md Success Criteria

1. Manage Channel + / − pills (no checkboxes)
   - Current: `#manage-human-pills`, `#manage-robot-pills`, `manage-invite-add` + buttons, pills rendered.
   - Old checkbox divs (`#manage-humans`, `#manage-robots`) still exist in DOM but appear secondary.
   - Status: mostly done, minor cleanup possible.

2. Composer mention pills (`@Happy` → pill, send posts nostr: + mention_N)
   - `#composer-pills`, `.composer-pill`, applyMention logic present.
   - Status: done.

3. Compact 24px glyphs for .chan-del and .robot-card .toggle / voice
   - `.chan-del, .chan-voice, .robot-call { width:24px; height:24px }`
   - Status: done.

4. Movable collapsible tool rail (#tool-rail, grip, drag, collapse, localStorage)
   - Full `<aside id="tool-rail">` with rail-bar, grip, toggle (☰), rail-body, install-help, profile/settings/call/invite/add-chan/providers in rail.
   - Header still contains some legacy #install etc in places (dupe?).
   - Status: largely implemented; need to confirm header no longer "hosts" the primary actions.

5. Whisper-gated Call / Voice icons
   - `whisperReady = !!st.whisper`
   - `robot-call`, `chan-voice`, `tile-mute` classes and JS gating present.
   - Status: done.

6. Tile mute in channel call
   - `.tile-mute` CSS + logic present.
   - Status: done.

7. Exit reaps tracked children + /proc sweep
   - `hush_relay_track_child` called from relay, agent, canvas, provider.
   - `hush_relay_reap_children` + `hush_child_sweep_proc`.
   - `check_exit.sh` creates fake --class=hush-relay --app child and asserts it is killed on /api/exit.
   - Status: implemented and tested.

8. make test + check greps pass
   - Baseline on this worktree: ALL TESTS PASSED.
   - check_launch.sh and check_exit.sh contain the required greps (tool-rail, composer-pill, robot-call, reap, etc.).
   - Status: passing.

9. Lifecycle (PR only, cleanup)
   - To be executed at end.

## Gaps found on this base (post all previous PRs)
- Header still renders a top-level Install button in some paths (plan wants primary actions *moved* to rail).
- Manage channel still has legacy `#manage-humans` / checkbox divs in HTML (pills are the active path).
- No new C changes needed for M3.1 — reap is already in place (plan's M3.1 was done in earlier slices).
- The plan was written against an older base; much of M3/M4 has been delivered incrementally.

## Decision for this worktree (atomic)
Since the heavy lifting (reap, pills, rail, voice gate) is present and tested, the atomic work here is:
- M1.1: Research gate + current-base audit (this doc).
- M4.x hygiene: clean legacy header buttons if they duplicate rail, clean old checkbox divs if dead, ensure all plan greps are satisfied.
- M5.1: Extend checks if needed, run full embed + test gate.
- M5.2: PR + full PRIME_DIRECTIVE lifecycle.

No large feature work; this is verification + polish atomic to close the plan.
