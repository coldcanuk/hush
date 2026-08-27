# PLAN: Co-mention follow-through, one intro, chaperon rails, channel events

**Branch:** `gb/chan-chat-rails`  
**Worktree:** `worktrees/chan-chat-rails`  
**Gate:** `docs/research/RESEARCH_CHAN_CHAT_RAILS.md`  
**Land:** PR to main only.

## Scope

Fix co-mention: one intro per robot+thread, Happy intro present, Major not twice, Major analyzes after Happy's joke. Name permutation rules. Channel chaperon + max robot turns. In-hive timed events.

## Non-goals

Voice; outbound HTTP webhooks; mention-order assembler; live LLM skill forge.

## Milestones

M1.1 Research (this + RESEARCH) — commit  
M3.1 consider_one + intro uniqueness + follow queue after robot notes  
M3.2 Prompt: per-robot assignment, no self-mention  
M3.3 Chaperon role + max_robot_turns rails  
M3.4 hush_cevent ring + GET /api/chan-events  
M3.5 Manage Channel UI, tests, PR  

## Verification

`test_intel.c` + `check_agent.sh` drive shipped consider/on_posted. Two live launches. `make test`.
