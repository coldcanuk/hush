---
name: write-legible-c
description: >
  Apply a strict C11 and repository-legibility standard when creating,
  modifying, refactoring, debugging, fixing, reviewing, or presenting C code.
  Use for .c and .h files, C APIs and snippets, tests written in C, build-facing
  C code, and agent-facing instructions or conventions for C repositories.
  Triggers: $write-legible-c, write legible C, legible C, C standard, AGENTS.md
  C rules, refactor C to standard, machine-legible C, C11 style, MODULE_TRY,
  section-16 map_eat. Do not use for C++, Objective-C, or general prose-only C
  questions that assess neither code nor a C repository.
metadata:
  short-description: "Enforce legible C and repository standards"
---

# Write Legible C

Codex port of 7etsuo/write-legible-c. Invoke with `$write-legible-c`, or
let Codex select it for C work. Resolve reference links relative to this
skill directory. Use Codex tools available in the session; no Grok CLI or
Grok plugin manager is required.

Apply the machine-legibility standard to every C region created, changed,
reviewed, or presented.

## Load the standard

Read [references/c-standard.md](references/c-standard.md) completely before
reasoning about a C change. Treat it as the normative implementation and
review checklist.

Use section 15 as the pattern for greenfield modules. Use section 16 as the
default pattern for editing existing code: diagnose the near miss, make the
smallest behavior-preserving decomposition, then decide whether the task's
scope permits the fully conforming API stage.

Use section 17 when assessing or changing a C repository's agent guidance,
conventions, directory documentation, or test feedback loop. Use section 18
to understand a rule's provenance, not as a substitute for repository-local
evidence.

If the workspace has a repo-root `AGENTS.md` that already embeds this standard
(or a subset), treat repo `AGENTS.md` as higher-priority for that workspace,
and still use this skill's full checklist for any rule the repo file omits.

## Work in this order

1. Read the applicable user instructions, `AGENTS.md` files, public headers,
   build rules, and tests before designing the change.
2. Map the touched module's ownership, public API, constants, types, static
   call graph, bounded loops, resources, status values, dispatch tables, and
   state invariants.
3. Preserve behavior and compatibility unless the user requests a semantic
   change. Use adapters when an external API conflicts with the standard.
4. Design the file-top vocabulary and function boundaries before editing
   bodies. Classify every function as an orchestrator, leaf, or adapter, then
   separately record whether it is a public boundary. Public visibility is
   not a fourth function altitude. Keep each helper at one altitude and
   require it to pass the name test in section 4 of the standard.
5. Implement the smallest coherent change. Apply the standard to every
   touched function and declaration, including test code written in C.
6. Run the project's required build and tests, then apply every item in the
   standard's pre-delivery checklist to the final diff. The checklist is a
   final gate, not a substitute for auditing every section.

Treat the prose rules as authoritative when an example is incomplete. Apply
the same gates to snippets and full files. Check declarations at their first
valid value, the abbreviation allowlist, output-parameter order,
internal-helper asserts, function classification, cognitive-complexity
budget, loop bounds, recursion ban, dereference depth, function-pointer
placement, and assertion density explicitly before delivery. Recheck every
state-mutating leaf for a precondition or postcondition assert.

For an existing function that already looks short and flat, run the section
16 near-miss test before accepting it. Look for duplicated mutation, data
encoded as control flow, interleaved concepts, and declarations placed before
their first valid value. Judge the refactor by the cost of the next change.

For repository-level work, keep instructions dense and evidence-based. Do
not invent a path map that can be discovered from the tree, and do not change
agent guidance or test policy unless the task authorizes repository-level
changes.

## Resolve constraints

- Follow higher-priority user, repository, ABI, wire-format, generated-code,
  and platform requirements when they conflict with this skill.
- Do not widen a scoped task into a repository-wide rewrite merely because
  nearby untouched C predates the standard.
- When a required constraint forces a deviation, add the required comment at
  the deviation site and state the constraint precisely.
- When a frozen public signature cannot return a status, preserve its behavior
  and place the deviation comment immediately above the affected declaration
  or definition, including in a standalone snippet. A delivery note does not
  replace the source-site comment. Do not let the legacy boundary excuse
  missing internal assertions or other locally satisfiable rules.
- Do not claim compliance when required checks could not run. Report the
  unchecked command or rule.

## Deliver

Report the behavior change, the structural changes that materially improve
legibility, the verification performed, and any documented deviations. Keep
the report shorter than the code review it summarizes.
