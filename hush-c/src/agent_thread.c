/* agent_thread.c: owns thread walking and conversation-context assembly. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent_internal.h"
#include "hush_roster.h"
#include "hush_seg.h"
#include "hush_store.h"
#include "hush_thread.h"

#define HUSH_AGENT_THREAD_HEAD \
    "Thread so far. Do not repeat a prior joke. " \

static size_t hush_agent_thread_skip(const hush_event_t *evs, size_t n,
                                    const char *root)
{
    size_t i;
    size_t kept;
    size_t start;

    assert(evs != NULL);
    assert(root != NULL);
    kept = 0;
    start = 0;
    for (i = 0; i < n; i++) {
        if (!hush_agent_event_is_root(&evs[i], root))
            continue;
        kept++;
        if (kept > (size_t)HUSH_AGENT_THREAD_MAX)
            start++;
    }
    return start;
}

static void hush_agent_push_thread(hush_event_t *out, size_t *count, const hush_event_t *event);

static void hush_agent_walk_thread(char *out, size_t outsz,
                                  const hush_event_t *evs, size_t n,
                                  const hush_agent_thread_walk_t *walk)
{
    size_t i;
    size_t start;
    size_t seen;
    const char *who;

    assert(out != NULL);
    assert(evs != NULL);
    assert(walk != NULL);
    assert(walk->root != NULL);
    assert(walk->human_pub != NULL);
    assert(walk->human != NULL);
    assert(walk->robot != NULL);
    start = hush_agent_thread_skip(evs, n, walk->root);
    seen = 0;
    for (i = 0; i < n; i++) {
        if (!hush_agent_event_is_root(&evs[i], walk->root))
            continue;
        if (seen < start) {
            seen++;
            continue;
        }
        who = walk->human;
        if (strcmp(evs[i].pubkey, walk->human_pub) != 0) {
            hush_agent_robot_t peer;
            if (walk->launch != NULL && hush_agent_lookup_robot(&peer, walk->launch, evs[i].pubkey))
                who = peer.name;
            else
                who = walk->robot;
        }
        hush_agent_append_turn(out, outsz, &evs[i], who);
        seen++;
    }
}

static size_t hush_agent_collect_thread(const hush_store_t *store, hush_event_t *out,
                                         const char *root, const char *trigger)
{
    assert(store != NULL && out != NULL);
    assert(root != NULL && trigger != NULL);
    size_t found = 0;
    size_t count = hush_store_count(store);
    for (size_t i = 0; i < count && i < (size_t)HUSH_STORE_CAPACITY; ++i) {
        hush_event_t event = {0};
        if (hush_store_get(store, i, &event) != HUSH_OK) break;
        if (event.kind != HUSH_AGENT_KIND_NOTE || strcmp(event.id, trigger) == 0 ||
            strcmp(event.id, root) == 0 || !hush_agent_event_is_root(&event, root) ||
            !hush_agent_is_work_note(event.content))
            continue;
        hush_agent_push_thread(out, &found, &event);
    }
    return found;
}

static void hush_agent_push_thread(hush_event_t *out, size_t *count, const hush_event_t *event)
{
    assert(out != NULL && count != NULL && event != NULL);
    assert(*count <= (size_t)HUSH_AGENT_THREAD_MAX);
    if (*count == (size_t)HUSH_AGENT_THREAD_MAX) {
        memmove(out, out + 1, (*count - 1) * sizeof(*out));
        --*count;
    }
    out[(*count)++] = *event;
}

/* Appends prefix + text as one line, rolling back on overflow. */
static void hush_agent_append_line(char *out, size_t outsz, const char *prefix,
                                   const char *text)
{
    size_t used;
    int written;

    assert(out != NULL && outsz > 0);
    assert(prefix != NULL);
    assert(text != NULL);
    used = strlen(out);
    if (used + 2 >= outsz)
        return;
    written = snprintf(out + used, outsz - used, "%s%s\n", prefix, text);
    if (written < 0 || (size_t)written >= outsz - used)
        out[used] = '\0';
}

/* Renders the durable transcript when the live store lost the thread. */
static void hush_agent_append_durable(char *out, size_t outsz,
                                      const hush_event_t *parent,
                                      const hush_agent_thread_walk_t *walk)
{
    hush_thread_turn_t turns[HUSH_AGENT_THREAD_MAX];
    size_t count;
    size_t i;

    assert(out != NULL && outsz > 0);
    assert(parent != NULL);
    assert(walk != NULL);
    assert(walk->root != NULL);
    count = hush_thread_read(walk->root, turns, HUSH_AGENT_THREAD_MAX);
    for (i = 0; i < count; ++i) {
        hush_agent_robot_t peer;
        const char *who = walk->robot;
        hush_event_t ev = {0};

        if (strcmp(turns[i].id, parent->id) == 0)
            continue;
        hush_agent_copy(ev.content, sizeof(ev.content), turns[i].content);
        if (strcmp(turns[i].pubkey, walk->human_pub) == 0)
            who = walk->human;
        else if (walk->launch != NULL &&
                 hush_agent_lookup_robot(&peer, walk->launch, turns[i].pubkey))
            who = peer.name;
        hush_agent_append_turn(out, outsz, &ev, who);
    }
}

/* Renders the thread: live store turns when present, durable otherwise. */
void hush_agent_fill_thread(char *out, size_t outsz,
                                  hush_store_t *store,
                                  const hush_launch_t *launch,
                                  const hush_event_t *parent,
                                  const hush_agent_thread_walk_t *names)
{
    assert(out != NULL && outsz > 0);
    assert(parent != NULL && names != NULL);
    out[0] = '\0';
    if (store == NULL)
        return;
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_agent_event_root(root, sizeof(root), parent);
    hush_event_t original = {0};
    const char *human = parent->pubkey;
    if (hush_store_find(store, &original, root) == HUSH_OK)
        human = original.pubkey;
    hush_event_t events[HUSH_AGENT_THREAD_MAX] = {0};
    size_t count = hush_agent_collect_thread(store, events, root, parent->id);
    hush_agent_thread_walk_t walk = *names;
    walk.launch = launch;
    walk.root = root;
    walk.human_pub = human;
    int owner = original.id[0] != '\0' && strcmp(original.id, parent->id) != 0;
    char brief[HUSH_THREAD_BRIEF_MAX + 1];
    char line[HUSH_AGENT_SNIP_MAX + 1];
    hush_agent_copy(out, outsz, HUSH_AGENT_THREAD_HEAD);
    hush_thread_brief_get(root, brief, sizeof(brief));
    hush_agent_snip_line(line, sizeof(line), brief);
    if (line[0] != '\0')
        hush_agent_append_line(out, outsz, "Thread brief: ", line);
    if (owner)
        hush_agent_append_turn(out, outsz, &original, "Conversation owner");
    if (count > 0)
        hush_agent_walk_thread(out, outsz, events, count, &walk);
    else if (!owner)
        hush_agent_append_durable(out, outsz, parent, &walk);
    size_t used = strlen(out);
    int written = snprintf(out + used, outsz - used, "\nCurrent message: %s", parent->content);
    if (written < 0 || (size_t)written >= outsz - used)
        hush_agent_copy(out, outsz, parent->content);
}

/* True when a context MIME is Markdown (chunk with fence awareness). */
static int hush_agent_is_markdown(const char *mime)
{
    if (mime == NULL)
        return 0;
    return strcmp(mime, HUSH_ROSTER_MIME_MARKDOWN) == 0 ||
           strcmp(mime, HUSH_ROSTER_MIME_XMARKDOWN) == 0;
}

/* Appends bounded, structurally-chunked file context to the robot note.
 * hush_seg splits each body so the first included chunk ends on a semantic
 * boundary (sentence/paragraph, or a markdown fence) rather than mid-word.
 * Stops when the note buffer is full. No-op when the robot has no files. */
void hush_agent_append_context(char *note, size_t notesz,
                                      const hush_agent_robot_t *bot)
{
    size_t off;
    size_t i;

    assert(note != NULL);
    assert(notesz > 0);
    assert(bot != NULL);
    off = strlen(note);
    for (i = 0; i < bot->ncontext && i < (size_t)HUSH_ROSTER_CONTEXT_MAX; i++) {
        const hush_roster_context_t *ctx = &bot->context[i];
        hush_seg_span_t span;
        size_t len;
        int n;

        if (off + 2 >= notesz)
            return;
        if (ctx->text[0] == '\0')
            continue;
        n = snprintf(note + off, notesz - off, "\n[file: %s]\n", ctx->name);
        if (n < 0 || (size_t)n >= notesz - off)
            return;
        off += (size_t)n;
        if (off + 2 >= notesz)
            return;
        if (hush_seg_split(ctx->text, ctx->bytes,
                           hush_agent_is_markdown(ctx->mime),
                           notesz - off - 1, notesz - off - 1,
                           &span, 1) != 1)
            continue;
        len = span.len;
        if (len > notesz - off - 1)
            len = notesz - off - 1;
        if (len == 0)
            return;
        memcpy(note + off, ctx->text + span.off, len);
        off += len;
        note[off] = '\0';
    }
}

