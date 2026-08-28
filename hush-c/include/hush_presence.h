/* hush_presence.h: NIP-38 presence line, trail kind, Stuck keep-alive. */

#ifndef HUSH_PRESENCE_H
#define HUSH_PRESENCE_H

#include <stddef.h>
#include <time.h>
#include "hush_event.h"
#include "hush_status.h"
#include "hush_store.h"

enum {
    HUSH_PRESENCE_KIND_LINE = 30315,
    HUSH_PRESENCE_KIND_TRAIL = 1038,
    HUSH_PRESENCE_SLUG_MAX = 48,
    HUSH_PRESENCE_D_MAX = 48,
    HUSH_PRESENCE_D_LEN = 45,
    HUSH_PRESENCE_D_HEX_LEN = 40,
    HUSH_PRESENCE_DIGEST_LEN = 32,
    HUSH_PRESENCE_DIGEST_D_BYTES = 20,
    HUSH_PRESENCE_LINES_MAX = 16,
    HUSH_PRESENCE_JSON_MAX = 8192,
    HUSH_PRESENCE_HEARTBEAT_S = 15,
    HUSH_PRESENCE_STALL_S = 30,
    HUSH_PRESENCE_IDLE_S = 45
};

#define HUSH_PRESENCE_D_PREFIX "hive:"
#define HUSH_PRESENCE_TRAIL_LEASE "lease-drop"
#define HUSH_PRESENCE_SLUG_BUILDING "Building"
#define HUSH_PRESENCE_SLUG_RESEARCHING "Researching"
#define HUSH_PRESENCE_SLUG_PLANNING "Planning"
#define HUSH_PRESENCE_SLUG_DEBUG_CODE "Debugging Code"
#define HUSH_PRESENCE_SLUG_CONVERSING "Conversing"
#define HUSH_PRESENCE_SLUG_BREEZE "Shooting the Breeze"
#define HUSH_PRESENCE_SLUG_WASTING "Wasting Tokens"
#define HUSH_PRESENCE_SLUG_STUCK "Stuck"
#define HUSH_PRESENCE_SLUG_WORKING "Working"
#define HUSH_PRESENCE_SLUG_WAITING "Waiting"
#define HUSH_PRESENCE_SLUG_IDLE "Idle"

typedef struct {
    const char *pubkey;
    const char *role;
    const char *slug;
    const char *channel;
    const char *root;
    time_t now;
} hush_presence_in_t;

/* Zeros the live line table. Safe twice. Does not touch the wake ledger. */
void hush_presence_init(void);

/* True when slug is in the closed table or Debugging <Thing>. */
int hush_presence_slug_ok(const char *slug);

/* True when role is not chaperon. NULL role is a worker. */
int hush_presence_role_ok(const char *role);

/* True when kind 30315/1038 may appear on NIP-01 REQ. */
int hush_presence_req_ok(uint32_t kind, int vibe_public);

/* Writes 32-byte SHA-256(lowercase robot_hex || ":" || lowercase root_hex).
 * robot_hex and root_hex must each be 64 hex characters. Fails HUSH_ERR_ARG
 * on NULL, wrong length, or non-hex. Fails HUSH_ERR_CRYPTO on digest failure.
 * out is written only on HUSH_OK. */
hush_status_t hush_presence_work_digest(unsigned char out[HUSH_PRESENCE_DIGEST_LEN],
                                        const char *robot_hex,
                                        const char *root_hex);

/* Writes d = hive: + lowercase hex of the first 20 bytes of the work digest.
 * 45 characters plus NUL. Fails HUSH_ERR_FULL if outsz is under 46.
 * "fN" process tokens must not be passed as robot_hex or root_hex. */
hush_status_t hush_presence_make_d(char *out, size_t outsz,
                                   const char *robot_hex,
                                   const char *root_hex);

/* Publishes 30315 (replace on pubkey,kind,d) + 1038 trail + cevent.
 * d is reconstructed from pubkey + root. Actor pubkey signs identity. */
hush_status_t hush_presence_publish(hush_store_t *store,
                                    const hush_presence_in_t *in);

/* Empty 30315 for the reconstructible d of pubkey+root. */
hush_status_t hush_presence_clear(hush_store_t *store, const char *pubkey,
                                  const char *root, const char *channel,
                                  time_t now);

/* Clears 30315 and appends 1038 content lease-drop for this pubkey+root. */
hush_status_t hush_presence_lease_drop(hush_store_t *store,
                                       const hush_presence_in_t *in);

/* Marks a beat on an existing line. Stall uses this clock. */
hush_status_t hush_presence_beat(const char *pubkey, const char *root,
                                 time_t now);

/* True when the line is Stuck and a keep-alive is due. */
int hush_presence_stuck_due(const char *pubkey, const char *root, time_t now);

/* Seconds since last beat, or -1 when the line is unknown. */
int hush_presence_stall_s(const char *pubkey, const char *root, time_t now);

/* Expires Idle/Waiting lines whose clock has run out. */
void hush_presence_expire(hush_store_t *store, time_t now);

/* JSON {"ok":true,"lines":[...]} of live 30315 rows. Operator view; not REQ. */
hush_status_t hush_presence_format_json(char *out, size_t outsz, size_t *out_len);

#endif /* HUSH_PRESENCE_H */
