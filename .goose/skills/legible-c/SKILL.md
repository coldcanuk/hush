---
name: legible-c
description: "Apply write-legible-c + c-standard §14 pre-delivery checklist to all .c/.h changes."
---
# legible-c

Before any C commit or PR:
1. Read references/c-standard.md (or loaded skill).
2. Run the 17-item checklist on every changed .c/.h:
   - Named literals (no magic >1)
   - Fn <=40 lines, depth <=2
   - No "and" in contracts
   - No goto (or justified)
   - Every fallible call checked
   - Prototypes match, at top
   - Error producers minimal
   - No pasted logic (extract)
   - <=4 params or struct
   - Header minimal
   - No mixed altitudes in fn
   - Helpers name concepts (inline paraphrases)
   - TRY only in non-acquiring fns
   - Param order: ctx, out, in
   - Loops have static bounds
   - No recursion
   - State leaves have asserts
3. Use MODULE_TRY where appropriate.
4. Rebuild + test after edits.

See write-legible-c skill.
