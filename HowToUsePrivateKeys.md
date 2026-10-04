# How to use private keys

Hush has three kinds of identity: you, Payne, and each robot you raise. Each one has a public id (what other people can see) and a private key (the nsec). After a restart, the same public ids come back only when the matching private key comes back from a store.

Hush cannot remember a private key by itself. There is no extra copy inside Hush. If none of the three stores has the key, a restart does not pretend the old identity returned.

This unlock does not use LastPass or Bitwarden. It does not add a new Hush vault. It does not write the private key into a plaintext file.

## Where the key lives

Put each private key in one of these stores. One store is enough.

1. `pass`, the Unix password store.
2. 1Password, through the `op` command.
3. The Pop!_OS keyring, through `secret-tool` (libsecret).

`pass` names:

- you: `hush/identity/nsec`
- Payne: `hush/agents/sgt-major-payne/nsec`
- a robot: `hush/agents/<agent name>/nsec`

The first-launch backup step can still save your key into `pass` when that box is checked. Payne and robots already had a `pass` path. This does not change that.

1Password uses a vault named `Hush`, unless you set `HUSH_OP_VAULT` to another vault name with no spaces. Each key is a Password item. The password field is the nsec. The item titles are:

- you: `hush-identity-nsec`
- Payne: `hush-agents-sgt-major-payne-nsec`
- a robot: `hush-agents-<agent name>-nsec`

Slashes in the `pass` path become hyphens in the item title, with `hush-` in front.

The Pop!_OS keyring uses two attributes: `service` is `hush`, and `item` is the path without the `hush/` prefix (`identity/nsec`, `agents/sgt-major-payne/nsec`, or `agents/<agent name>/nsec`).

## How to unlock after a restart

Start Hush the way you usually do. On startup it reads `pass`, then 1Password, then the keyring. The first store that returns a private key unlocks that identity. The public id is calculated from that key, so it matches the id from before the restart.

You do not type the key into a new file for this. Hush does not write a plaintext private key to do this unlock.

You can check a store yourself:

```bash
pass show hush/identity/nsec
op read 'op://Hush/hush-identity-nsec/password'
secret-tool lookup service hush item identity/nsec
```

`op` has to already be signed in. Hush does not invent a new key when `op` is locked or missing.

To put a key into 1Password, create a Password item in the `Hush` vault with the title above and paste the nsec into the password field. To put a key into the keyring, run this and type the nsec at the prompt (it is not read from a file Hush writes):

```bash
secret-tool store --label='Hush identity/nsec' service hush item identity/nsec
```

Use the Payne or robot path in that same shape when the key belongs to them.

## If a store is missing

If `pass`, `op`, or `secret-tool` is not installed, or the store has no key for that identity, Hush keeps the restart behavior it already had.

- Your login stays logged out. The session says `restart_lost_login` when a vibe is already saved. That is the honest path. Hush does not create a new human key and present it as you.
- An older home file (`agents/<agent name>/nsec`) is leftover. It is not a place to keep a key. A private key is never written to a plain file.
- When none of the three stores has the key, a new robot key stays in memory until pass, 1Password, or the keyring is chosen. That key has a new public id. It is not the old one. It is not written to a plain file.

Hush will not mint a new identity and call it the old one.
