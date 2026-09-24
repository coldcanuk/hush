/* hush_thread.h: durable per-thread transcripts and rolling briefs.
 *
 * Context-budget policy (WS5 M1): the live ring is preferred, the durable
 * transcript fills only window slots the ring cannot, and the rolling brief
 * carries the pinned summary. Per root, newest wins: up to
 * HUSH_AGENT_THREAD_MAX newest ring turns (each snipped to
 * HUSH_AGENT_SNIP_MAX bytes), durable turns backfill uncovered slots
 * oldest-available-first, and every published robot reply rolls one
 * HUSH_THREAD_ROLL_SNIP_MAX-byte snip onto the brief, evicting from the
 * front past HUSH_THREAD_BRIEF_MAX. The job note assembles brief, opening,
 * backfill, ring turns, then the verbatim current message. */

#ifndef HUSH_THREAD_H
#define HUSH_THREAD_H

#include <stddef.h>

#include "hush_event.h"
#include "hush_json.h"
#include "hush_status.h"

enum {
    /* Content kept per transcript turn; longer notes are truncated. */
    HUSH_THREAD_CONTENT_MAX = 2048,
    /* Rolling brief cap. */
    HUSH_THREAD_BRIEF_MAX = 2048,
    /* One rolled answer contributes at most this many flattened bytes. */
    HUSH_THREAD_ROLL_SNIP_MAX = 200,
    /* Turns a single read returns at most. */
    HUSH_THREAD_TURNS_MAX = 32,
    /* One turns[] frame: escaped content plus id/pubkey/at framing. */
    HUSH_THREAD_TURN_JSON = HUSH_THREAD_CONTENT_MAX * HUSH_JSON_U_LEN + 256,
    /* Largest thread-memory body: every turn plus the brief plus framing. */
    HUSH_THREAD_JSON_MAX = HUSH_THREAD_TURNS_MAX * HUSH_THREAD_TURN_JSON +
        HUSH_THREAD_BRIEF_MAX * HUSH_JSON_U_LEN + 512
};

typedef struct {
    char id[HUSH_EVENT_ID_HEX_LEN + 1];
    char pubkey[HUSH_EVENT_PUBKEY_HEX_LEN + 1];
    int64_t created_at;
    char content[HUSH_THREAD_CONTENT_MAX + 1];
} hush_thread_turn_t;

/* Appends one kind-1 note to its root transcript under $HUSH_HOME/threads.
 * Best effort: ignores non-notes, event storage failures, and invalid roots. */
void hush_thread_record(const hush_event_t *ev);

/* Copies the newest turns for root, oldest first. Returns the count written,
 * which is at most max and at most HUSH_THREAD_TURNS_MAX. */
size_t hush_thread_read(const char *root, hush_thread_turn_t *out, size_t max);

/* Copies the rolling brief for root into out. Empty when none. */
void hush_thread_brief_get(const char *root, char *out, size_t outsz);

/* Replaces the rolling brief for root. Empty text removes it. */
void hush_thread_brief_set(const char *root, const char *text);

/* Rolls one flattened snip of text onto the root's brief, evicting the
 * oldest entries past HUSH_THREAD_BRIEF_MAX. Blank text and invalid roots
 * are no-ops; never removes an existing brief. */
void hush_thread_brief_roll(const char *root, const char *text);

/* Number of transcript turns for root. */
size_t hush_thread_count(const char *root);

/* Formats the durable memory for root as one JSON object: ok, root, brief,
 * count (every stored turn), truncated (count exceeds turns[]), and turns[]
 * (newest HUSH_THREAD_TURNS_MAX, oldest first). Unknown roots format an
 * honest empty object. Fails with ARG on a bad root or output, FULL when
 * outsz cannot hold the body. */
hush_status_t hush_thread_format_json(const char *root, char *out,
                                      size_t outsz, size_t *out_len);

#endif /* HUSH_THREAD_H */
