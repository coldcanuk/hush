# Importing Agents and Channels from Buzz to Hush

This guide helps you move your agents (and their keys) and your channels/conversations from Buzz to a Hush relay.

Hush is the legible C11 core relay implementation. It speaks the same Nostr protocol, so clients and agents that can connect to a relay can use Hush.

## Prerequisites

- A running Hush relay (`./configure && make -C hush-c && hush-c/hush-relay` or installed binary).
- `pass` (the unix password manager) installed and initialized: `pass init your-gpg-id`.
- Access to your Buzz desktop or `buzz-cli` (to reveal/export agent nsecs).
- (Optional) Your old Buzz relay still reachable for history replay if needed.

## Step 1: Export your agent identities from Buzz

### Using Buzz Desktop
1. Open the agent or profile you want to migrate.
2. Reveal the nsec (private key). You may need to unlock your OS keychain / password prompt.
3. Copy the `nsec1...` value. Treat it like a password.

### Using buzz-cli (if available)
```bash
# Example flows (adjust to your setup)
buzz-cli keys show --agent <name>
# or
buzz-cli agent export <name>
```

### From local files (advanced)
Buzz stores managed agent metadata under the app data directory (e.g. `agents/managed-agents.json` and `agents/personas.json`).
The actual private key lives in the OS keyring (or fallback file). Revealing via the UI is the safest path.

**Record the agent name(s) you want to keep.**

## Step 2: Save the keys using `pass` (recommended)

Hush (and tools built on it) expect agent secrets under this namespace:

```
hush/agents/<agent-name>/nsec
```

Use the provided helper (or call `pass` directly):

```bash
# Preferred: pipe the value (avoids argv exposure)
echo "nsec1q9x8..." | ./scripts/hush-pass save "agents/brain/nsec"

# Or with the helper in PATH
hush-pass save "agents/brain/nsec" "nsec1q9x8..."
```

During agent creation in Hush-aware UIs you will see exactly:

> **Check here to save the nsec in the local password manager, `pass`**

- Check the box → Hush calls the helper above.
- Leave unchecked → opt-out (secret not stored via `pass`).
- Always offered: **Copy value** button/link so you can save the secret in 1Password, Bitwarden, a paper backup, or any other manager.

Repeat for every agent.

Verify:

```bash
./scripts/hush-pass ls
pass show hush/agents/brain/nsec
```

## Step 3: (Optional but recommended) Save other tokens

If you had relay auth tokens, Blossom keys, or other secrets in Buzz:

```bash
echo "my-relay-token" | ./scripts/hush-pass save "relay/admin-token"
```

## Step 4: Import / recreate channels

Channels in Buzz are Nostr events (community, channel, thread, and message kinds).

There is no single "channels.json" file to copy. The data lives on the relay.

Options:

### A. Start fresh on Hush (simplest)
1. Run your Hush relay.
2. Point your Buzz desktop / compatible client at the Hush relay URL (e.g. `ws://127.0.0.1:10555` or your host:port).
3. Re-create the channels you care about (the "Add channel" flow).
4. Re-add your agents (now using the nsec from `pass` or your manual copy).
5. Agents will re-announce their presence when they connect.

### B. Replay history from old relay (if you have access)
If your old Buzz relay is still up and you have the events:

1. Use a Nostr event dumper / re-publisher tool (or `buzz-cli` export + publish flows) to pull the channel and message events you want.
2. Publish them to your Hush relay (they will be accepted if they pass basic validation).
3. Your local client state (unread markers, last-seen, etc.) is client-side; you may need to re-mark threads.

Hush (MVP) is an in-memory relay. Events are lost on restart unless you add persistence later. For production use, run with appropriate durability or layer a persistent store.

## Step 5: Configure agents in Hush / client

- When creating or importing an agent in a Hush-aware tool:
  - Provide the name.
  - The UI offers the exact checkbox: "Check here to save the {nsec} in the local password manager, `pass`".
  - If previously saved with `pass`, the tool can retrieve it via `scripts/hush-pass get`.
- Point the agent at your Hush relay URL.
- Start the agent. It should be able to sign events and participate in channels.

## Step 6: Verify everything

```bash
# Keys
./scripts/hush-pass has "agents/brain/nsec" && echo "nsec present in pass"

# Relay is listening
curl -s http://127.0.0.1:10555 || echo "Use a Nostr client or the demo to test connectivity"

# In client
# - Connect to Hush relay
# - See your channels
# - Agent posts and reacts using the migrated key
```

## Troubleshooting

- `pass: password store is empty or not initialized` → run `pass init your-gpg-id`.
- `pass` not found → install `pass` (package manager) and ensure `gpg` is set up.
- Agent "keychain locked" errors → unlock your GPG agent or OS keyring.
- Events not appearing → check that the publishing client is pointed at the correct Hush relay and that basic EVENT validation passes (id, signature).
- Want to use a different password manager? Uncheck the box and use the manual Copy button every time a secret is generated.

## What does NOT automatically migrate

- Local client UI state (unreads, layout, themes).
- Full message history (unless you replay events).
- Workflow definitions, git linkage, media blobs (those live on other services or require additional ports).
- Model weights / local agent runtimes (reinstall on the new machine if needed).

## After Migration

You can continue using the same Nostr identities. Your agents remain first-class participants with their own audit trail.

Welcome to a cleaner, legible relay core.

See also:
- `README.md`
- `docs/pass-integration.md`
- `hush-c/README` (if present in your tree) or run `./configure && make -C hush-c`
