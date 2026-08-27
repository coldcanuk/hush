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
    HUSH_PRESENCE_LINES_MAX = 16,
    HUSH_PRESENCE_JSON_MAX = 8192,
    HUSH_PRESENCE_HEARTBEAT_S = 15,
    HUSH_PRESENCE_STALL_S = 30,
    HUSH_PRESENCE_IDLE_S = 45
};

#define HUSH_PRESENCE_D_PREFIX "hive:"
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
    const char *token;
    const char *slug;
    const char *channel;
    const char *root;
    time_t now;
} hush_presence_in_t;

/* Zeros the live line table. Safe twice. */
void hush_presence_init(void);

/* True when slug is in the closed table or Debugging <Thing>. */
int hush_presence_slug_ok(const char *slug);

/* True when role is not chaperon. NULL role is a worker. */
int hush_presence_role_ok(const char *role);

/* True when kind 30315/1038 may appear on NIP-01 REQ. */
int hush_presence_req_ok(uint32_t kind, int vibe_public);

/* Writes d = hive:<token> into out. */
hush_status_t hush_presence_make_d(char *out, size_t outsz, const char *token);

/* Publishes 30315 (replace) + 1038 trail + cevent. Actor pubkey signs identity. */
hush_status_t hush_presence_publish(hush_store_t *store,
                                    const hush_presence_in_t *in);

/* Empty 30315 for this d. */
hush_status_t hush_presence_clear(hush_store_t *store, const char *pubkey,
                                  const char *token, const char *channel,
                                  time_t now);

/* Marks a beat on an existing line. Stall uses this clock. */
hush_status_t hush_presence_beat(const char *token, time_t now);

/* True when the line is Stuck and a keep-alive is due. */
int hush_presence_stuck_due(const char *token, time_t now);

/* Seconds since last beat, or -1 when the token is unknown. */
int hush_presence_stall_s(const char *token, time_t now);

/* Expires Idle/Waiting lines whose clock has run out. */
void hush_presence_expire(hush_store_t *store, time_t now);

/* JSON {"ok":true,"lines":[...]} of live 30315 rows. */
hush_status_t hush_presence_format_json(char *out, size_t outsz, size_t *out_len);

#endif /* HUSH_PRESENCE_H */
