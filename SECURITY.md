# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you discover a security vulnerability in Hush, please report it privately via
[GitHub Security Advisories](https://github.com/coldcanuk/hush/security/advisories/new)
or by contacting the maintainers listed in `debian/control`. Include as much
detail as possible:

- A description of the vulnerability and its potential impact
- Steps to reproduce or a proof-of-concept (if available)
- The affected version(s) or commit range
- Any suggested mitigations you've identified

You will receive an acknowledgment within **48 hours**. We aim to provide a
full response — including a timeline for a fix — within **7 days** of initial
contact. We'll keep you informed as we work toward a resolution.

We ask that you:

- Give us reasonable time to address the issue before any public disclosure
- Avoid accessing or modifying data that does not belong to you
- Not perform denial-of-service attacks or disrupt production systems

We will credit reporters in release notes unless you prefer to remain anonymous.

---

## Supported Versions

| Version | Supported |
|---------|-----------|
| `main` (latest) | ✅ Active |
| Previous releases | ⚠️ Best-effort; upgrade recommended |

Hush is pre-1.0. We do not maintain long-term support branches at this stage.
All security fixes land on `main` first.

---

## Security Design Principles

### Authentication — NIP-42 (planned)

The Hush MVP accepts newline-delimited Nostr frames over TCP and does not yet
enforce NIP-42. Production deployments that need authentication should treat
the listener as a trusted-network service until AUTH lands.

The intended model is
[NIP-42](https://github.com/nostr-protocol/nips/blob/master/42.md)
challenge/response before writing events: the relay sends a random challenge;
the client signs a `kind:22242` event containing the challenge and the relay
URL, proving possession of the private key.

### Authorization — Channel Membership as the Gate

Channel membership is the **only** access control mechanism once AUTH is
enabled. There are no separate ACL lists or capability taxonomies. If a
principal (human or agent) is a member of a channel, they can read and write
to it. If they are not a member, the relay rejects their requests — even if
they are authenticated.

Private channels are invisible to non-members: they do not appear in channel
listings, and subscription filters for private channel events return nothing
unless the subscriber is a member.

A **vibe** (this relay) has the same visibility: `public` vibes are
discoverable and joinable; `private` vibes are not listed and require the
operator’s join token. Full NIP-42 AUTH is still planned — the MVP hides
listings and issues a token, it does not yet challenge every socket.

### STUN/TURN

The optional coturn child/daemon is started with a generated long-term
credential (`user=hush:<random>`). Never run an open TURN relay: it will be
used as a DDoS reflector. TLS/DTLS for TURN is out of this slice; put
coturn behind a firewall and set `external-ip` when NATed. Daemon mode
installs a systemd unit but does not enable it until the operator asks.

### In-Memory Store (MVP)

The MVP store is a bounded in-memory ring. Events are not durable across
process restart and are not written to a tamper-evident audit log. Do not
treat a running `hush-relay` as a compliance archive.

### Agent Secret Storage — `pass`

Hush-aware tools store secrets in the unix password manager `pass` **by
default**. The human must uncheck the modal box to opt out.

| Secret | Path | Retrieve |
|---|---|---|
| Human identity | `hush/identity/nsec` | `pass show hush/identity/nsec` |
| Agent nsec | `hush/agents/<agent-name>/nsec` | `pass show hush/agents/<agent-name>/nsec` |

See [IMPORT.md](IMPORT.md) and [docs/pass-integration.md](docs/pass-integration.md).

The `HUSH_PRIVATE_KEY` environment variable, when set, takes precedence for
harnessed agents and CI.

### Input Validation

- Event ids, pubkeys, and signatures are fixed-length hex buffers.
- Content and tag strings are bounded (`HUSH_EVENT_MAX_CONTENT`,
  `HUSH_EVENT_MAX_TAGS`, `HUSH_EVENT_MAX_TAG_LEN`).
- The wire parser rejects malformed lines instead of trusting client input.
- Filter arrays have static caps (`HUSH_FILTER_MAX_KINDS` and related
  constants).

### Transport Security

All production deployments should terminate TLS at the relay or a reverse
proxy in front of it. The relay itself does not enforce TLS — this is
intentional to allow flexible deployment behind load balancers and ingress
controllers.

### Build Hardening

Hush is strict C11. The required compiler flags are
`-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`. There is no Rust,
Cargo, or `unsafe` crate surface in this repository.

---

## Disclosure Policy

We follow [coordinated disclosure](https://en.wikipedia.org/wiki/Coordinated_vulnerability_disclosure).
Reporters will be credited unless they request anonymity.
