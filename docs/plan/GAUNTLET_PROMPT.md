# Goose Instruction Prompt: The Hush Gauntlet

**Primary Directive for Goose:** You are to execute the "Gauntlet Loop" on the Hush application. You have access to Playwright and an instruct visual model. Follow this protocol precisely.

## Phase 1: Compilation and Onboarding
1. **Compile Hush**: Initialize the repository and compile the Hush C11 Nostr relay core (e.g., `./configure`, `make`, `make test`). Start the application.
2. **Visual Interaction**: Launch Playwright to interact with the Hush interface. 
3. **Onboarding Walkthrough**: Step through the first-time Wizard and complete the onboarding process as a new user. Explore and utilize the interface thoroughly.
    * **CRITICAL OVERRIDE**: Your active model (DeepSeek) does not support images natively. You MUST NOT attach images to the chat. To "see" the UI, take screenshots with Playwright, save them to disk, and analyze them by executing this exact command in your shell: `python3 .goose/skills/vision/vision_tool.py <path_to_screenshot> "Describe this UI in detail..."` This script securely delegates the vision task to a dedicated visual model and returns the text description back to you.

## Phase 2: The Full Shake Audit
Perform a complete audit of the application based on your visual and functional walkthrough. Evaluate and score the application on a scale of **0 to 10** (0 = Absolute worst/broken, 10 = Flawless perfection) across the following categories:
*   **User Experience (UX)**
*   **Ease of Use**
*   **Entertainment Value**
*   **Functionality**
*   **Productive Value**

For each category, provide the precise score and a detailed justification based on your findings.

## Phase 3: Backlog and Build Plan
1. **Take Count**: Inventory all flaws, broken elements, areas needing modification, and any ideas you have for making the app better.
2. **Formulate a Plan**: Create a step-by-step build plan to address this inventory. This plan will guide your building and improvement phase. Ensure you adhere strictly to the Hush `AGENTS.md` and `PRIME_DIRECTIVE.md` rules (e.g., using worktrees, branching strategies).

## Phase 4: The Gauntlet Loop & Verification
You must now execute the build plan in a continuous iteration loop ("The Gauntlet"). **You are forbidden from exiting this loop until all category scores reach a predefined threshold of perfection (e.g., 9.5+).**

### The Skeptic and The Critic (Verification Protocol)
For every change, modification, or fix made during the Gauntlet Loop, you must pass the verification protocol before the score can be increased:
1. **Default Stance**: Adopt the personas of the **Skeptic** and the **Critic**. They approach the entire work with the baseline assumption that *everything is broken and everything is a lie*.
2. **Burden of Proof**: Claims like "the bug is fixed" or "the UI is improved" are rejected by default. Hard evidence (Playwright screenshots, DOM snapshots, network logs, and test assertions) is strictly required. 
    * **Note on Screenshots**: Use `python3 .goose/skills/vision/vision_tool.py <path_to_screenshot>` to evaluate screenshots.
3. **Bayesian Evaluation**: The Critic will use a Bayesian framework to evaluate the theory ($H$) that the fix/feature is successfully implemented given the provided evidence ($E$):
    *   **Prior $P(H)$**: Start with a very low probability (e.g., 5% or 0.05) that the change actually works as intended, reflecting the Skeptic's stance.
    *   **Likelihood $P(E|H)$**: If the feature works perfectly, how likely is the evidence provided (e.g., the screenshot shows the exact correct alignment)?
    *   **False Positive Rate $P(E|\neg H)$**: If the feature is still broken, how likely is it that we'd see this evidence anyway (e.g., a test passes but it's a flaky test or testing the wrong thing)?
    *   **Posterior $P(H|E)$**: Calculate the updated probability using Bayes' Theorem. 
    *   **Acceptance**: The fix is only accepted, and the iteration deemed successful, if the posterior probability exceeds 95% ($>0.95$).

Iterate through **Plan -> Build -> Skeptic/Critic Verification -> Re-Score** until the application survives the Gauntlet.
