/* hush_wake.h: bounded durable claim ledger for one robot on one root. */

#ifndef HUSH_WAKE_H
#define HUSH_WAKE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "hush_status.h"
#include "hush_store.h"

enum {
    /* Same value as HUSH_AGENT_TIMEOUT_S in hush_agent.h. Not
     * HUSH_PRESENCE_STALL_S (beat gap, 30s). Stuck UI may not expire;
     * this claim lease must. */
    HUSH_WAKE_LEASE_S = 90,
    HUSH_WAKE_SLOT_MAX = 256,
    HUSH_WAKE_KEY_LEN = 32,
    HUSH_WAKE_TRIGGER_LEN = 32,
    HUSH_WAKE_DEVICE_LEN = 16,
    HUSH_WAKE_DEVICE_HEX_LEN = 32,
    HUSH_WAKE_VERSION = 1,
    /* Regular kind. Claim gossip, not NIP-38. Not an offline signal. */
    HUSH_WAKE_KIND_CLAIM = 1039
};

#define HUSH_WAKE_FILE "wake.ledger"
#define HUSH_WAKE_DEVICE_FILE "device.id"
#define HUSH_WAKE_MAGIC 0x314B5748u

typedef enum {
    HUSH_WAKE_ST_EMPTY = 0,
    HUSH_WAKE_ST_INTRO = 1,
    HUSH_WAKE_ST_CLAIMED = 2,
    HUSH_WAKE_ST_DONE = 3
} hush_wake_state_t;

typedef struct {
    hush_store_t *store;
    const char *robot_hex;
    const char *root_hex;
    const char *trigger_id;
    const char *channel;
    time_t now;
} hush_wake_in_t;

/* Loads device id and the slot file under hush home. Creates both when
 * missing. Safe twice. Does not mint a new device id when the file exists.
 * This file is the lock, not an index for find_d and not a speedup.
 * Work slots are keyed by SHA-256(robot || ":" || root). Delivery slots in
 * the same file are keyed by SHA-256(robot || ":" || trigger id). */
void hush_wake_init(void);

/* True when ledger state for (robot, root) is intro, claimed, or done. */
int hush_wake_intro_seen(const char *robot_hex, const char *root_hex);

/* Records intro for (robot, root). Idempotent when already intro/claimed/done.
 * Fsyncs. Fails HUSH_ERR_ARG, HUSH_ERR_FULL, HUSH_ERR_IO. */
hush_status_t hush_wake_mark_intro(const hush_wake_in_t *in);

/* Takes or reclaims work. Claim before spawn. Fsyncs on take and reclaim.
 * empty → take. claimed+same device+live lease → reclaim. claimed+other
 * device+live lease → HUSH_ERR_DENIED. expired lease → 30315 clear, 1038
 * lease-drop, then take. done+same trigger → HUSH_ERR_DENIED. done+new
 * trigger → take. */
hush_status_t hush_wake_claim(const hush_wake_in_t *in);

/* Marks done for (robot, root, trigger). Fsyncs. Clears no 30315; caller
 * must hush_presence_clear. Fails HUSH_ERR_ARG, HUSH_ERR_IO. */
hush_status_t hush_wake_done(const hush_wake_in_t *in);

/* For each claimed slot whose lease has passed: clear 30315, 1038
 * lease-drop, mark done so the same trigger cannot restart. Fsyncs if any
 * slot changed. */
void hush_wake_expire(hush_store_t *store, time_t now);

/* Slot state, or HUSH_WAKE_ST_EMPTY when the key is absent. */
hush_wake_state_t hush_wake_state(const char *robot_hex, const char *root_hex);

/* Replaces the process device id in RAM only. Does not rewrite device.id.
 * id must be HUSH_WAKE_DEVICE_LEN bytes. Test hook for two-device claims. */
void hush_wake_test_set_device(const unsigned char *id);

/* Applies a peer kind-1039 claim event to the ledger. Own device is ignored.
 * Does not publish. Does not read 30315. */
hush_status_t hush_wake_ingest(const hush_event_t *ev);

/* Ingests every kind-1039 currently in store (after persist load). */
void hush_wake_ingest_store(const hush_store_t *store);

#endif /* HUSH_WAKE_H */
