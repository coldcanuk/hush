/* hush_thread.h: durable per-thread transcripts and rolling briefs. */

#ifndef HUSH_THREAD_H
#define HUSH_THREAD_H

#include <stddef.h>

#include "hush_event.h"
#include "hush_status.h"

enum {
    /* Content kept per transcript turn; longer notes are truncated. */
    HUSH_THREAD_CONTENT_MAX = 2048,
    /* Rolling brief cap. */
    HUSH_THREAD_BRIEF_MAX = 2048,
    /* Turns a single read returns at most. */
    HUSH_THREAD_TURNS_MAX = 32
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

/* Number of transcript turns for root. */
size_t hush_thread_count(const char *root);

#endif /* HUSH_THREAD_H */
