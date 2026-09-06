# Goose Instruction Prompt: The Hush Gauntlet

**Primary Directive for Goose:** Execute the "Gauntlet Loop" on the Hush application. You have access to Playwright and an instruct visual model. Follow this protocol precisely.

**Scoring law:** Use the category algebra and pass curve in [`GAUNTLET_LOOP_PROMPT.md`](GAUNTLET_LOOP_PROMPT.md). Summary: scale 0–10; **KEEP GOING** while any applicable score \(< 7.8\); **ACCEPTABLE PASS** when all applicable scores \(\ge 7.8\) and composite \(S \ge 7.8\). Aspire to 9+, but never require universal 9s — that incentivizes lying.

## Phase 1: Compilation and Onboarding
1. **Compile Hush**: Initialize the repository and compile the Hush C11 Nostr relay core (`./configure`, `make`, `make test`). Start the application.
2. **Visual Interaction**: Launch Playwright to interact with the Hush interface.
3. **Onboarding Walkthrough**: Step through the first-time Wizard and complete onboarding as a new user. Explore and utilize the interface thoroughly.
   * **CRITICAL OVERRIDE**: If the active model does not support images natively, do **not** attach images to the chat. To "see" the UI, take screenshots with Playwright, save them to disk, and analyze them with: `python3 .agents/skills/vision/vision_tool.py <path_to_screenshot> "Describe this UI in detail..."`. That script delegates vision to a dedicated model and returns text.

## Phase 2: The Full Shake Audit
Perform a complete audit from the visual and functional walkthrough. Score **0 to 10** (0 = worst/broken, 5 = usable but buggy, 10 = perfection) on:

### Product shake categories (audit lens)
* **User Experience (UX)**
* **Ease of Use**
* **Entertainment Value**
* **Functionality**
* **Productive Value**

Also fill the **engineering scorecard** (C1–C10) from `GAUNTLET_LOOP_PROMPT.md` whenever you change code.

For each category: precise one-decimal score + justification with evidence.

Apply the same pass curve: soft-miss \(< 7.8\) → keep going; band 7.8–8.9 → acceptable; 9+ → aspire only.

## Phase 3: Backlog and Build Plan
1. **Take Count**: Inventory flaws, broken elements, needed modifications, and genuine improvements.
2. **Formulate a Plan**: Step-by-step build plan. Prefer fixing HARD FAIL and soft-miss categories first. Adhere to `AGENTS.md` and `PRIME_DIRECTIVE.md` (worktrees, `gb/<slug>`, PR-only land).

## Phase 4: The Gauntlet Loop & Verification
Execute the build plan in a continuous iteration loop ("The Gauntlet").

**Exit rule:** You may exit when **ACCEPTABLE PASS** is met per `GAUNTLET_LOOP_PROMPT.md` (all applicable \(s_i \ge 7.8\), \(S \ge 7.8\), compile/tests green, zero phantoms). You are **forbidden** from exiting while any applicable score is \(< 7.8\) or while compile/tests/phantoms fail.

You are also **forbidden** from staying in the loop solely to push honest 8.x scores to 9.x. That is score theater.

### The Skeptic and The Critic (Verification Protocol)
For every change during the Gauntlet Loop, pass verification before any score may increase:
1. **Default Stance**: Personas **Skeptic** and **Critic** assume everything is broken and every claim is a lie until proven.
2. **Burden of Proof**: Claims like "the bug is fixed" or "the UI is improved" are rejected by default. Hard evidence required (Playwright behavior checks, DOM snapshots, network logs, test assertions). Screenshots via `python3 .agents/skills/vision/vision_tool.py <path_to_screenshot>` are supporting \(E\), not sufficient alone.
3. **Bayesian Evaluation**: Critic evaluates theory \(H\) ("fix/feature works") given evidence \(E\):
   * Prior \(P(H)\): start very low (≈ 0.05).
   * Likelihood \(P(E|H)\): if it works, how likely is this evidence?
   * False positive rate \(P(E|\neg H)\): if still broken, how often would we see this evidence anyway?
   * Posterior \(P(H|E)\): Bayes update.
   * **Acceptance**: score may rise only if posterior \(> 0.95\). If evidence is weaker than before, **lower** the score.

Iterate **Plan → Build → Skeptic/Critic Verification → Re-Score** until **ACCEPTABLE PASS**, then stop.
