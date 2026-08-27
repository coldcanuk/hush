# Research: Robot Thread Bugs

## 1. The "Double Intro" Bug
The user reported that Major introduces himself twice.
- **Observation:** In `hush_agent.c`, robot intros are tracked per `(robot hex, thread root)` in the `g_intro_hex` and `g_intro_root` arrays via `hush_agent_intro_seen()` and `hush_agent_intro_remember()`.
- **Root Cause:** A "conversation" in the UI is conceptually a session, but the C backend tracks intros strictly per *Nostr thread root* (the original note ID of a thread). If the user tests multiple threads in the same session, the backend treats each thread root as a new context and re-issues the intro "At ease...".
- **Evidence:** `UI_SPEC.md` states: `Robot intros ("At ease...") appear at most once per robot per session`. The backend fails this requirement by scoping to `thread root`.

## 2. The Task Execution Bug
The user commanded: `@Happy generate a riddle and let @Major answer the riddle.`
- **Observation:** Happy generated a riddle AND answered it, then handed off to Major. Major then generated a NEW riddle AND answered it.
- **Root Cause 1 (The Ask):** In group mentions, `hush_agent_follow_push()` captures the original human `ev->content` as `slot->ask`. When Major is later kicked, it is passed the EXACT SAME original ask.
- **Root Cause 2 (The Prompt):** `HUSH_AGENT_PROMPT_FALLBACK` says `"Fulfill the last human ask in one note."` Because both robots receive the same ask, they both try to fulfill the entire command instead of recognizing their specific role.
- **Root Cause 3 (Thread Walking):** In `hush_agent_walk_thread()`, the code attributes ANY non-human note to the CURRENT robot (`walk->robot`). `if (strcmp(evs[i].pubkey, walk->human_pub) != 0) who = walk->robot;` This makes Major think it was the one who said Happy's response.
