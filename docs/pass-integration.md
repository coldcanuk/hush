# Hush + `pass` Integration

Hush uses the standard unix password manager `pass` whenever a human or agent needs to persist keys, tokens, or passwords.

## Exact UI/UX Contract

When the application offers to save a secret, it **must** present:

> **Check here to save the {password/key/token/etc} in the local password manager, `pass`**

- Checked (default recommended for most users): call `scripts/hush-pass save "category/name/type" "value"`
- Unchecked (opt-out): do **not** call pass. The secret stays in memory for the session or uses any other configured backend (e.g. OS keyring fallback in desktop).
- **Always** offer a manual copy path:
  - "Copy value (save in another password manager of your choice)"
  - Display the secret once (one-time view / clipboard button) and let the human copy it.

This contract applies to:
- Agent private keys (nsec)
- Relay auth tokens
- Any other Hush-managed passwords or API secrets

## Namespace Convention

All Hush secrets live under the `hush/` prefix in the pass store:

```
hush/
  agents/
    <agent-name>/
      nsec
  relay/
    admin-token
  tokens/
    ...
```

## Helper Script

`scripts/hush-pass` is the single portable entry point.

```bash
# Recommended: pipe to avoid argv exposure
echo "nsec1..." | scripts/hush-pass save "agents/brain/nsec"

# Retrieve
scripts/hush-pass get "agents/brain/nsec"

# Check existence (for UIs)
if scripts/hush-pass has "agents/brain/nsec"; then ...; fi

# List
scripts/hush-pass ls
```

If `pass` is not installed or the store is not initialized, the script prints clear instructions and still supports the manual copy path.

## Agent Creation Flow (example)

1. User clicks "Create Agent" or equivalent.
2. Hush generates or accepts an nsec.
3. Dialog shows:
   - [x] Check here to save the nsec in the local password manager, `pass`
   - [Copy nsec] (manual save to 1Password, Bitwarden, paper, etc.)
4. If checked → `scripts/hush-pass save "agents/<name>/nsec" "$NSEC"`
5. If unchecked → secret is used for current session only (or stored via desktop keyring if that path is active).
6. User can later `pass show hush/agents/<name>/nsec` or re-import.

## Security Notes

- Never log the secret value.
- Prefer pipe input (`echo ... | hush-pass save ...`) so the value does not appear in process arguments.
- `pass` relies on GPG; the user must have run `pass init <gpg-id>` at least once.
- On headless servers without a GPG agent, consider `pass` with appropriate `PASSWORD_STORE_GPG_OPTS` or document the limitation.

## Opt-out and Portability

Users who do not want `pass` can:
- Leave the checkbox unchecked.
- Manually copy the value into any other manager.
- Use Hush without persistent secret storage (ephemeral mode for testing).

This design fulfills the requirement: Hush will use `pass` when saving keys/tokens/passwords, while always providing manual copy and a clear opt-out.
