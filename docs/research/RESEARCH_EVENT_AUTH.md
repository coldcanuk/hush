# RESEARCH: Event authentication (schnorr / NIP-42) for Hush 0.0.1

**Scope.** Verify the external review's event-authentication claims against the code, map the current
key/identity/id machinery, assess the crypto environment, and recommend an implementation path.
**Revision verified.** Worktree `worktrees/review-hardening` on branch `gb/review-hardening` @
`180ab61d628389509ddb5e981bddb1c386ad9cba`. `git diff 5f67c65eb..HEAD` over the files below is
**empty**, so every citation also holds for the reviewed commit `5f67c65eb`.
**Method.** Static reading of the sources + one out-of-repo feasibility probe compiled in `/tmp`
(the repository was **not** built and its tests were **not** run). Environment commands and their
outputs are quoted in section (c). Web citations in section (g).
**Related in-repo work.** `docs/research/REVIEW_HUSH_0.0.1.md:57` is the review text being checked;
`docs/research/RESEARCH_SECURITY_SURFACE.md:403-404, 569-571` already flags the same gap from the
HTTP-gate angle.

---

## (a) Claim verification

| # | Claim | Verdict | Evidence |
|---|-------|---------|----------|
| 1 | `hush_event_t` has no `sig` field (hush_event.h:20-29); `hush_event_validate()` exists but is never called outside tests. | **verified** | See 1a-1d. |
| 2 | The wire parser sscanf's a claimed pubkey and stores it (hush_proto.c:127, hush_relay.c:549); the relay answers `["OK", id, true]` without signature or id verification. | **verified** | See 2a-2e. |
| 3 | `hush_intel.c:603` treats "author is human" as the branch that unlocks robot dispatch. | **partially verified** | Line and branch are exactly as stated, but the gate compares claimed pubkey **strings** and is only reachable from the HTTP POST path, which already force-assigns the local human pubkey. See 3a-3e. |

### 1a. No signature field

`hush-c/include/hush_event.h:20-29`:

```c
typedef struct {
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    char pubkey[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    uint32_t kind;
    int64_t created_at;
    char content[HUSH_EVENT_MAX_CONTENT + 1];
    size_t tag_count;
    char tags[HUSH_EVENT_MAX_TAGS][HUSH_EVENT_MAX_TAG_ELEMS][HUSH_EVENT_MAX_TAG_LEN + 1];
} hush_event_t;
```

### 1b. The signature constant exists but is dead

`hush-c/include/hush_event.h:13` declares `HUSH_EVENT_SIG_HEX_LEN = 128`. A whole-worktree grep for
`HUSH_EVENT_SIG_HEX_LEN` returns **only that declaration** - no consumer.

### 1c. No signing/verification code anywhere

Grep of `hush-c` for `schnorr|ECDSA|EC_KEY|DigestSign|EVP_PKEY|BIP340|bip340` returns a single hit:
`hush-c/src/hush_identity.c:183` (`EC_GROUP_new_by_curve_name(NID_secp256k1)`, used for pubkey
derivation only). No signature generation, no verification, no key-agreement use.

### 1d. `hush_event_validate` is test-only

`hush-c/src/hush_event.c:95-106` defines it; whole-worktree grep for `hush_event_validate` finds:
`hush-c/src/hush_event.c:95` (definition), `hush-c/include/hush_event.h:38` (declaration) and
`hush-c/tests/test_event.c:88,90,93` (the only callers). The header says so itself at
`hush_event.h:37`: *"Basic structural validation (lengths, kind bounds). Does not verify signature."*

### 2a. Claimed pubkey is read with `strstr` + `sscanf` and stored verbatim

`hush-c/src/hush_proto.c:120-135`:

```c
static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out)
{
    ...
    const char *idp = strstr(s, "\"id\":\"");
    if (idp) sscanf(idp + 6, "%64[^\"]", out->id);
    const char *pp = strstr(s, "\"pubkey\":\"");
    if (pp) sscanf(pp + 10, "%64[^\"]", out->pubkey);
    const char *kp = strstr(s, "\"kind\":");
    if (kp) out->kind = (uint32_t)atoi(kp + 7);
    const char *cp = strstr(s, "\"content\":\"");
    if (cp) sscanf(cp + 11, "%4096[^\"]", out->content);
    out->created_at = 1720000000;
    return HUSH_OK;
}
```

`created_at` is **hard-coded** (`:133`), tags are never parsed, `sig` is never read, and
`sscanf` caps at 64 chars without requiring exactly 64. Consequence: even if the relay recomputed
ids today, a real client event could never match because the preimage's timestamp/tags are wrong.

### 2b. The line ingest path stores, acks `true`, and fans out unconditionally

`hush-c/src/hush_relay.c:545-554`:

```c
static void hush_handle_event_msg(struct client *c, const hush_client_msg_t *msg)
{
    char line[HUSH_BUF_SZ];

    (void)hush_store_insert(g_store, &msg->event);
    (void)hush_wake_ingest(&msg->event);
    if (hush_proto_format_ok(msg->event.id, 1, "", line, sizeof(line), NULL) == HUSH_OK)
        hush_send_str(c->fd, line);
    hush_fanout(&msg->event);
}
```

No `hush_event_validate`, no `hush_event_compute_id`, no signature check, no authorization check on
the path that reaches this function (`hush_on_nostr_line` `hush_relay.c:518-530`).

### 2c. The OK format

`hush-c/src/hush_proto.c:84`: `snprintf(out_buf, bufsz, "[\"OK\",\"%s\",%s,\"%s\"]\n", ev_id, ok ? "true" : "false", msg);`
with `ok=1` at `hush_relay.c:551`.

### 2d. `hush_event_compute_id` is never called on the wire path

Grep of `hush_event_compute_id` callers: `hush_http.c:736,2466`, `hush_intel.c:485`,
`hush_launch.c:1402,1418,1441`, `hush_wake.c:1014`, `hush_roster.c:750`, `hush_agent.c:1142`,
`hush_presence.c:543` - all server-generated events. The raw newline ingest path is absent.

### 2e. `hush_store_insert` performs no validation

`hush-c/src/hush_store.c:136-149` only replaces addressable events and writes/appends; no id, sig,
or structural check.

### 3a. The cited branch

`hush-c/src/hush_intel.c:603-606`:

```c
if (!hush_intel_is_human(launch, ev->pubkey) && ch->robot_hops == 0) {
    hush_intel_post_line(store, ev, hex, HUSH_INTEL_DENY_HOP);
    return 1;
}
```

A human-claimed `pubkey` passes; a robot-claimed pubkey passes when `robot_hops != 0`.
Dispatch itself is `hush_intel_consider` (`hush_intel.c:112-132`) → `hush_intel_handle_robot`
(`hush_intel.c:130, 637+`).

### 3b. "Human" is string equality against local config

`hush-c/src/hush_intel.c:273-289` compares the claimed `pubkey` against
`launch->human.pubkey_hex` (when `launch->logged_in`) and every `launch->roster.members[i].pubkey_hex`.
There is no proof of key possession anywhere in the check.

### 3c. Dispatch is HTTP-only

Whole-tree grep for `hush_intel_consider` callers: only `hush-c/src/hush_http.c:743` (plus tests).
The raw newline EVENT path (2b) never dispatches robots.

### 3d. The HTTP path already forces the human pubkey

`hush-c/src/hush_http.c:721-748` requires `g_launch->logged_in` (`:724`) and
`hush_http_parse_note` assigns `memcpy(out->pubkey, g_launch->human.pubkey_hex, sizeof(out->pubkey));`
(`:763`). So on the only reachable dispatch path, `hush_intel_is_human` is true by construction.

### 3e. Why this still matters

`logged_in` is not a credential: `POST /api/identity` with `{"action":"create"}` sets it with no
secret (`hush_http.c:965-975` → `hush_launch_create_identity`, `hush_launch.c:429-439`,
`launch->logged_in = 1;`). Any client that can reach the HTTP port can therefore make itself "the
human" and unlock dispatch. The branch is an identity-string/process-state gate, never cryptographic -
which is the review's point, even though the raw wire path cannot reach it today.

---

## (b) Current key / identity / event-id code map

### Identity creation and import

| Function | Location | Behaviour |
|---|---|---|
| `hush_identity_generate` | `hush-c/src/hush_identity.c:34-49` | `RAND_bytes` 32 bytes, retry up to 8x, derive |
| `hush_identity_import` | `hush-c/src/hush_identity.c:51-79` | accepts `nsec1…` (bech32) or 64-char hex |
| `hush_identity_pubkey_xonly` | `hush-c/src/hush_identity.c:171-207` | `EC_GROUP_new_by_curve_name(NID_secp256k1)` (:183), `EC_POINT_mul(group, pub, priv, NULL, NULL, NULL)` (:192), affine (x,y), `BN_bn2binpad(x, out32, 32)` (:196) → x-only NIP-01 pubkey |
| `hush_identity_derive` | `hush-c/src/hush_identity.c:153-169` | fills pubkey, nsec, npub, pubkey_hex |
| `hush_identity_clear` | `hush-c/src/hush_identity.c:81-86` | `memset` entire struct |
| `hush_bech32_encode/decode` | `hush-c/src/hush_bech32.c:53-92` / `:94+` | NIP-19 **bech32** (not bech32m; comment :1), 32-byte payload, 6-char checksum (gens :12-16) |

### Where the nsec lives

- Primary store is **`pass`** through a helper: `hush-c/src/hush_pass.c` shells out to
  `hush-pass` (default, :13) or `../scripts/hush-pass` when present (:14); the helper runs
  `pass insert -m -f hush/<path>` / `pass show hush/<path>` (`scripts/hush-pass:29-43`).
- Keys: `HUSH_PASS_IDENTITY_NSEC = "identity/nsec"`, `HUSH_PASS_PAYNE_NSEC = "agents/sgt-major-payne/nsec"`
  (`hush-c/include/hush_pass.h:16-17`); robot keys are `agents/<slug>/nsec`
  (`hush-c/src/hush_launch.c:2672`, `hush-c/src/hush_roster.c:783`).
- Human lifecycle: create `hush_launch.c:429-440`; import `:442-459`; save on backup-ack
  `:461-475` → `hush_launch_try_save(HUSH_PASS_IDENTITY_NSEC, launch->human.nsec)` `:1891-1907`;
  restore at startup `:477-499` (`hush_pass_has/get` → `hush_identity_import`), invoked from
  `hush_relay.c:624`.
- The nsec is also returned to the local UI session JSON until backup is acknowledged
  (`hush_launch.c:1611, 1626-1627`) and can be imported over HTTP (`hush_http.c:976-981`).
- There is **no key file** and no signing: keys are only used to derive npub/pubkey and to be
  displayed/stored.

### Event id

- `hush_event_compute_id` `hush-c/src/hush_event.c:33-92`: streams
  `[0,"<pubkey>",<created_at>,<kind>,[<tags>],"<content>"]` into `EVP_sha256`, hex-encodes.
  Escaping `:148-194` (`\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`, plus `\u00XX` for other C0).
- NIP-01 conformance, independently checked (see (c)): the three pinned ids in
  `hush-c/tests/test_event.c:47,64,75` reproduce exactly with `sha256sum` on the canonical
  preimages. Hush's serializer is correct for those shapes.
- Known deviations/gaps:
  1. **Empty tag elements are dropped**: `hush_event.c:66-75` does
     `if (ev->tags[i][j][0] == '\0') continue;` - a legal tag such as `["e",""]` serializes
     differently from NIP-01.
  2. C0 control characters outside `\b\f\n\r\t` are escaped as `\u00XX` (`:180-187`).
     NIP-01 says "all other characters must be included verbatim"; JS-style serializers do escape
     them, so this matches common clients but is not literally the NIP text.
  3. Representational caps (kind ≤ 65535 at `hush_event.c:101`, 32 tags, 4 elements/tag, 256 chars,
     4096-byte content) mean larger real events cannot be canonicalised.
  4. The **wire parser never populates `created_at` or `tags`** (2a), so id recomputation on
     ingested events cannot match until the parser is fixed.

---

## (c) Crypto environment findings

### Commands and outputs (run on this machine)

```
$ openssl version -a | head -2
OpenSSL 3.0.13 30 Jan 2024 (Library: OpenSSL 3.0.13 30 Jan 2024)

$ openssl ecparam -list_curves | grep -i secp256k1
secp256k1 : SECG curve over a 256 bit prime field

$ openssl ecparam -name secp256k1 -genkey -noout | head -2
-----BEGIN EC PRIVATE KEY-----
MHQCAQEEIBxEB6PLs+bXn8O7OZw/OGg3wFnYDlLh0fkgds/jdQ/XoAcGBSuBBAAK     # secp256k1 OID 1.3.132.0.10

$ ldconfig -p | grep -i secp            -> no matches
$ pkg-config --exists libsecp256k1      -> not found
$ ls /usr/include/secp256k1*            -> No such file or directory

$ openssl list -signature-algorithms    -> RSA, DSA, ED25519, ED448, SM2, ECDSA, HMAC, SIPHASH,
                                           POLY1305, CMAC   (no Schnorr, no BIP-340)
$ openssl list -providers               -> default provider only (3.0.13)
```

### What Hush already links

- `hush-c/Makefile:8`: `LDFLAGS := -lcrypto`.
- `configure:159-174`: probes `/usr/include/openssl/bn.h` (fallback `/usr/include/node/openssl`),
  sets `OPENSSL_LIBS="-lcrypto"` (fallback `-l:libcrypto.so.3`), and reports `MISSING openssl` otherwise.
- Net: **OpenSSL libcrypto is already a hard, detected dependency**; no libsecp256k1 anywhere.

### Feasibility probe: BIP-340 verify on OpenSSL BIGNUM/EC only (out of repo, in `/tmp`)

A ~200-line probe implemented BIP-340 `Verify` with nothing but `EVP_sha256`, `BN_*`, `EC_GROUP`/
`EC_POINT`/`BN_CTX`. It was compiled and run against the official 19-row vector CSV:

```
$ gcc -std=c11 -Wall -Wextra -O2 -o /tmp/bip340_probe /tmp/bip340_probe.c -lcrypto
COMPILE_EXIT=0                     # no warnings
$ /tmp/bip340_probe /tmp/bip340_vectors.csv
vector  0 expect=TRUE  got=TRUE  OK      ... vector 18 expect=TRUE  got=TRUE  OK
rows=19 pass=19 fail=0
timing: 200 verifies = 58.207 ms total, 291.0 us/op
```

All 19 official vectors pass, including every negative case:
pk not on curve (5), odd-Y `R` (6), negated message (7), negated `s` (8), `R` infinite (9,10),
`r` not an X coordinate (11), `r = p` (12), `s = n` (13), pubkey `x ≥ p` (14).
Vector 0's message is 32 zero bytes - the same shape as a Nostr event id.

The **core only** (hex helper + tagged challenge hash + `lift_x` + `verify`) is
**135 lines / 5 518 bytes** and compiles clean under the repo's strict flag set:

```
$ gcc -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Wno-unused-function -O2 -c hush_schnorr_verify.c
STRICT_CORE_COMPILE_EXIT=0
```

API status on 3.0.13: `EC_GROUP_get_curve`, `EC_POINT_mul`, `EC_POINT_get_affine_coordinates`,
`EC_POINT_set_affine_coordinates`, `BN_mod_sqrt` are all non-deprecated;
`EC_POINT_set_compressed_coordinates` also compiles under
`-Werror=deprecated-declarations` (tested in the same session).

### Independent id cross-check

`sha256sum` of the three canonical preimages reproduces the ids pinned in `tests/test_event.c`
byte-for-byte (minimal: `57353dc6…a4b2`, tagged: `3932fb39…06f4`, escaped: `2757d051…141d`).

---

## (d) Options

| Option | New dependency | Size / complexity | Portability (Debian / FreeBSD / OpenBSD) | Testability | Verdict |
|---|---|---|---|---|---|
| **A. In-repo verify with OpenSSL BIGNUM/EC** | none (`-lcrypto` already linked, `Makefile:8`, `configure:159-174`) | ~135-line core / 5.5 KB + hex wrapper + parser work; ~291 µs/op (untuned, per-call `BN_CTX`/`EC_GROUP`) | libcrypto exists on all three (OpenBSD base `libcrypto`; its port must add `crypto` to `WANTLIB`, today `WANTLIB = c` at `openbsd/net/hush-relay/Makefile:23`) | all 19 official vectors pass now; vectors are public domain/CC0-able | **recommended** |
| **B. Link libsecp256k1** | mandatory system lib | audited, fastest; adds packaging deps | Debian `libsecp256k1-dev` (trixie 0.5.0-2); FreeBSD `math/secp256k1`; NetBSD `security/libsecp256k1`; **no OpenBSD port found** (openports.pl `/path/security/libsecp256k1` and `/path/math/libsecp256k1` both 404 - unconfirmed) | reference-grade | breaks "no new mandatory dependency" |
| **B'. Vendor libsecp256k1** | none at build time | 12 `.c` + 92 headers, 5.37 MB total `src/`; verify subset ≈21 files/228 KB **plus** `precomputed_ecmult.c` 2.41 MB and `precomputed_ecmult_gen.c` 260 KB; MIT | historically OpenBSD build fixes needed upstream (bitcoin#19559) | reference-grade but huge review surface | reject |
| **C. Vendored single-file micro-verifier** | none | no maintained, clearly-licensed single-file C BIP-340 verifier was found; the closest hit (`metalicjames/BIP340`) is a teaching repo, license not reported by the GitHub API | unknown | unknown | reject; riskier than A with no dependency win |
| **D. OpenSSL EVP/provider** | - | no Schnorr in 3.0.13; ED25519 is a different curve/scheme | - | - | not applicable |

Reference core (this is the exact shape that passed 19/19; Hush naming applied):

```c
static int hush_lift_x(EC_GROUP *group, BN_CTX *ctx, const BIGNUM *x, EC_POINT *out)
{
    BIGNUM *p = BN_new(), *c = BN_new(), *y = BN_new(), *t = BN_new(), *seven = BN_new();
    int ok = 0;
    if (p == NULL || c == NULL || y == NULL || t == NULL || seven == NULL) goto out;
    if (EC_GROUP_get_curve(group, p, NULL, NULL, ctx) != 1) goto out;
    if (BN_cmp(x, p) >= 0) goto out;                        /* x >= p fails */
    if (BN_mod_sqr(c, x, p, ctx) != 1) goto out;
    if (BN_mod_mul(c, c, x, p, ctx) != 1) goto out;
    if (BN_set_word(seven, 7) != 1) goto out;
    if (BN_mod_add(c, c, seven, p, ctx) != 1) goto out;     /* c = x^3 + 7 */
    if (BN_mod_sqrt(y, c, p, ctx) == NULL) goto out;        /* no sqrt => off curve */
    if (BN_mod_sqr(t, y, p, ctx) != 1) goto out;
    if (BN_cmp(t, c) != 0) goto out;
    if (BN_is_odd(y) && BN_mod_sub(y, p, y, p, ctx) != 1) goto out;   /* even y */
    if (EC_POINT_set_affine_coordinates(group, out, x, y, ctx) != 1) goto out;
    ok = 1;
out:
    BN_free(seven); BN_free(t); BN_free(y); BN_free(c); BN_free(p);
    return ok;
}
/* R = s*G - e*P = s*G + (n-e)*P; reject infinity, odd Y, x(R) != r. */
```

---

## (e) RECOMMENDED approach, test-vector plan, acceptance criteria

### Recommended approach

**Phase 1 (authentication): in-repo BIP-340 verification on the OpenSSL primitives already linked.**

1. New legible-C module `hush-c/src/hush_schnorr.c` + `hush-c/include/hush_schnorr.h` exposing
   `hush_status_t hush_schnorr_verify(const char *pubkey_hex, const char *id_hex, const char *sig_hex);`
   (or 32-byte-array form). Implementation exactly as the 135-line core: tagged `BIP0340/challenge`
   hash, `lift_x` via `BN_mod_sqrt`, canonicality checks before arithmetic, `EC_POINT_mul`,
   infinity/parity/x checks. No new `LDFLAGS`.
2. Extend the event model: add `char sig[HUSH_EVENT_SIG_HEX_LEN + 1]` to `hush_event_t`
   (constant already exists at `hush_event.h:13`) and make `hush_proto_parse_event_object` parse
   `created_at` and `tags` faithfully (strict 64/64/128-hex length checks). Bump the store format
   version (`hush_store.c` `HUSH_STORE_VERSION`) because `hush_event_t` grows.
3. In `hush_handle_event_msg`: run `hush_event_validate` → recompute id and compare with the claimed
   id → `hush_schnorr_verify(ev.pubkey, ev.id, ev.sig)` → only then insert/ack/fanout. Reject with
   `["OK", id, false, "invalid: bad signature"]` (NIP-20 prefix) and never store/fanout/dispatch.
4. **Do not** fold NIP-42 gating into the same change: land verification first (small, testable),
   then AUTH as a second milestone.

Rationale: zero new dependencies, matches the "no new mandatory dependency" preference, works on
Debian/FreeBSD/OpenBSD (libcrypto ships with all three), and the exact algorithm is already proven on
this machine against the official vectors. The main cost is review of ~150 lines of crypto code, which
is bounded and fully vector-testable - far cheaper to review than a 1.5-5 MB vendored library.

### Test-vector plan

1. **Official vectors**: vendor `bip-0340/test-vectors.csv` (19 rows) under
   `hush-c/tests/vectors/bip340_test_vectors.csv` (or embed as a C array), with a provenance comment
   and its SHA-256; add `tests/test_schnorr.c` asserting the expected outcome of every row (4 valid
   groups + 11 negative cases listed in (c)). Wire into `make test`.
2. **Boundary cases**: `pk_x = p`, `r = p`, `s = n`, `pk`/`sig` all-zero and all-`0xFF`,
   `R = ∞` (vectors 9/10), `x` off-curve (5/11) - must fail cleanly, no crash.
3. **Nostr shape**: vector 0 already has a 32-byte message; additionally pin a real signed event
   (id/pubkey/sig triple from an independent client such as `nak`/nostr-tools) as a fixture, and keep
   the three `sha256sum`-verified id vectors in `tests/test_event.c` as a NIP-01 regression.
4. **Protocol-level**: forged `["EVENT",…]` line → `["OK", id, false, …]`, absent from
   `hush_store_query`, not fanned out; a valid line → stored + fanned out.
5. **Strictness**: `make test` clean under `-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`;
   `ldd hush-relay` unchanged.

### Exact acceptance criteria

1. `hush_schnorr_verify` returns `HUSH_OK` for every TRUE vector and a non-OK status for every FALSE
   vector; the test fails if any single row differs.
2. Non-canonical inputs are rejected **before** arithmetic: `pk_x ≥ p`, `r ≥ p`, `s ≥ n`,
   off-curve `pk_x`, `R` infinite, odd-Y `R`.
3. For every accepted wire EVENT, `hush_event_compute_id` equals the claimed `id`; mismatches are
   rejected with `"invalid: id mismatch"`.
4. No event is stored, fanned out, or passed to `hush_intel_consider` before both checks pass.
5. `git grep hush_event_validate hush-c/src` is non-empty (validator is on the hot path, not test-only).
6. Store version bumped; existing `make test` suite green; no new link dependency; OpenBSD port
   `WANTLIB` updated to `c crypto` (today it omits `crypto` although `-lcrypto` is used).
7. CPU-budget guard: verification (~300 µs/op) runs before storage; per-connection cap on
   unverified/accepted lines (drop beyond N per second) so the single-threaded poll loop cannot be
   pinned by a flood of invalid events.

---

## (f) NIP-42 AUTH design sketch for the line protocol

NIP-42 shape (see (g)): relay sends `["AUTH", <challenge>]`; client answers
`["AUTH", <signed kind-22242 event>]` carrying `["relay", <url>]` and `["challenge", <challenge>]`
tags; relay answers with `["OK", id, true|false, …]`; `auth-required: ` / `restricted: ` are the
machine-readable OK/CLOSED prefixes; relays must never broadcast or serve kind 22242; check kind,
fresh `created_at` (~10 min), matching challenge and relay URL, then the signature.

Mapping onto Hush (newline JSON arrays over the same poll(2) socket; dispatch at
`hush_relay.c:518-530`, per-connection state in `struct client` `hush_relay.c:81-90`):

1. **Challenge on connect.** In `hush_accept_new`, generate 32 random bytes (`RAND_bytes`), hex-encode,
   send `["AUTH","…"]`, and store it in the client struct.
2. **Parser.** Add `HUSH_MSG_AUTH` to `hush_msg_type_t` (`hush_proto.h:11-17`); recognize both
   `["AUTH","…"]` (server form, ignore) and `["AUTH",{event}]` (client form) in `hush_proto_parse_line`.
3. **Validation.** Reuse the EVENT parser plus the new verifier: `kind == 22242`,
   `|now − created_at| ≤ 600`, `challenge` tag equals the per-connection value, `relay` tag matches the
   listener URL/host, reassembled id equals recomputed id, signature valid. On success set
   `authed_pubkey` on the client; always answer with OK (`["OK", id, true, ""]` or
   `["OK", id, false, "auth-required: …"]`).
4. **Never persist kind 22242.** `hush_handle_event_msg` returns after the OK for that kind (NIP-42
   requires exclusion from broadcast).
5. **Policy.** Unauthenticated REQ → `["CLOSED", sub, "auth-required: …"]` (a formatter mirroring
   `hush_proto_format_eose` is needed); unauthenticated EVENT → OK false with the same prefix.
   After AUTH, accept events whose `pubkey` equals `authed_pubkey` (or is a roster/local member).
   NIP-42 explicitly permits "accept any events as long as they are published from an authenticated
   user", which fits Hush's local-human model.
6. **Local UI.** Loopback/HTTP-origin connections already carry `logged_in`; optionally treat them as
   authenticated (`authed_pubkey = g_launch->human.pubkey_hex`) so the existing browser flow needs no
   AUTH round trip, while raw TCP clients must AUTH.
7. **Compatibility.** Clients that ignore unknown `AUTH` lines keep working as long as the default
   policy stays permissive for reads; verification of every EVENT is unconditional.

---

## (g) Cited URLs

- BIP-340 spec - https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki
- BIP-340 official test vectors - https://raw.githubusercontent.com/bitcoin/bips/master/bip-0340/test-vectors.csv
- NIP-01 (event id canonical serialization) - https://github.com/nostr-protocol/nips/blob/master/01.md
- NIP-42 (client authentication) - https://github.com/nostr-protocol/nips/blob/master/42.md
- libsecp256k1 (MIT) - https://github.com/bitcoin-core/secp256k1
  (file sizes via https://api.github.com/repos/bitcoin-core/secp256k1/git/trees/master?recursive=1)
- Debian `libsecp256k1-dev` - https://packages.debian.org/sid/libsecp256k1-dev ;
  source 0.5.0-2 - https://packages.debian.org/stable/source/libsecp256k1
- FreeBSD `math/secp256k1` - https://www.freshports.org/math/secp256k1
- NetBSD `security/libsecp256k1` - https://ftp.netbsd.org/pub/pkgsrc/current/pkgsrc/security/libsecp256k1/index.html
- Bitcoin Core OpenBSD/libsecp256k1 build issue - https://github.com/bitcoin/bitcoin/issues/19559
- OpenSSL 3.0 EC/BN API references - https://docs.openssl.org/3.0/man3/EC_POINT_new/ ,
  https://docs.openssl.org/3.0/man3/BN_mod_sqrt/
- metalicjames/BIP340 (teaching implementation; GitHub API reports no license) -
  https://github.com/metalicjames/BIP340

### Unverifiable / open

- **OpenBSD libsecp256k1 port**: not confirmed. openports.pl returns 404 for both
  `/path/security/libsecp256k1` and `/path/math/libsecp256k1`; only NetBSD pkgsrc was positively
  identified. Do not rely on a system libsecp256k1 for OpenBSD without checking the live ports tree.
- **Throughput comparison** with libsecp256k1 was not measured; the only measured figure here is the
  OpenSSL probe's ~291 µs/op.
- **Single-file C verifier search**: the absence of a maintained, licensed single-file implementation
  is an absence of evidence from web search, not a proof that none exists.
