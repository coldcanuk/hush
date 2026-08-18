# Unix `pass` integration

Hush stores secrets in the standard unix password manager
[`pass`](https://www.passwordstore.org/). Saving is **on by default**.
The human must uncheck the box to opt out.

## Paths

| Secret | Store path | Retrieve |
|---|---|---|
| Human identity nsec | `hush/identity/nsec` | `pass show hush/identity/nsec` |
| Agent nsec | `hush/agents/<slug>/nsec` | `pass show hush/agents/<slug>/nsec` |
| Provider API key | `hush/providers/<id>/api_key` | `pass show hush/providers/<id>/api_key` |
| Provider username | `hush/providers/<id>/username` | `pass show hush/providers/<id>/username` |
| Provider password | `hush/providers/<id>/password` | `pass show hush/providers/<id>/password` |
| Provider token | `hush/providers/<id>/token` | `pass show hush/providers/<id>/token` |
| Provider passkey | `hush/providers/<id>/passkey` | `pass show hush/providers/<id>/passkey` |
| Other token | `hush/<category>/<name>` | `pass show hush/<category>/<name>` |

Helper (same namespace, path without the `hush/` prefix):

```sh
echo "nsec1…" | ./scripts/hush-pass save identity/nsec
./scripts/hush-pass get identity/nsec
./scripts/hush-pass has identity/nsec
./scripts/hush-pass ls
```

## Modal contract

Whenever Hush generates or first-shows a secret:

1. A modal shows the value (masked by default) with **Copy**.
2. A checkbox is **checked**:
   `Checked to save password to Unix Password Manager. Retrieve with: pass show hush/identity/nsec`
3. Confirming while checked writes the secret via `hush-pass` / `pass insert`.
4. Unchecking skips `pass`. Copy still works.
5. Missing or uninitialized `pass` never blocks identity creation.

## Prerequisites

```sh
# Debian / Ubuntu / Pop!_OS
sudo apt install pass gnupg

# then once
pass init your-gpg-id
```

See also [IMPORT.md](../IMPORT.md) and [SECURITY.md](../SECURITY.md).
