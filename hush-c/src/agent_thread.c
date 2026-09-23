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

/* Window slots the live ring cannot fill. All pointers borrowed. */
typedef struct {
    const hush_event_t *ring;
    size_t nring;
    size_t room;
    const char *root;
    const char *parent_id;
    int owner_shown;
} hush_agent_backfill_t;

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

/* Renders the rolled brief line when the root has one. */
static void hush_agent_append_brief(char *out, size_t outsz, const char *root);

/* Writes the stored opening note into original and returns its author's
 * pubkey, or the parent's pubkey when the root has left the ring. */
static const char *hush_agent_resolve_owner(hush_store_t *store,
                                            hush_event_t *original,
                                            const char *root,
                                            const hush_event_t *parent);

/* True when the durable turn belongs in the job note: not the parent
 * trigger, not an already-shown opening, not already held by the ring. */
static int hush_agent_backfill_wants(const hush_agent_backfill_t *fill,
                                     const hush_thread_turn_t *turn);

/* Renders one durable turn under the walk's display name. */
static void hush_agent_append_backfill_turn(char *out, size_t outsz,
                                            const hush_agent_thread_walk_t *walk,
                                            const hush_thread_turn_t *turn);

/* Appends the verbatim current message, replacing the note on overflow. */
static void hush_agent_append_current(char *out, size_t outsz,
                                      const hush_event_t *parent);

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

/* Renders the rolled brief line when the root has one. */
static void hush_agent_append_brief(char *out, size_t outsz, const char *root)
{
    char brief[HUSH_THREAD_BRIEF_MAX + 1];
    char line[HUSH_AGENT_SNIP_MAX + 1];

    assert(out != NULL && outsz > 0);
    assert(root != NULL);
    hush_thread_brief_get(root, brief, sizeof(brief));
    hush_agent_snip_line(line, sizeof(line), brief);
    if (line[0] != '\0')
        hush_agent_append_line(out, outsz, "Thread brief: ", line);
}

/* Writes the stored opening note into original and returns its author's
 * pubkey, or the parent's pubkey when the root has left the ring. */
static const char *hush_agent_resolve_owner(hush_store_t *store,
                                            hush_event_t *original,
                                            const char *root,
                                            const hush_event_t *parent)
{
    assert(store != NULL && original != NULL);
    assert(root != NULL && parent != NULL);
    if (hush_store_find(store, original, root) == HUSH_OK)
        return original->pubkey;
    return parent->pubkey;
}

/* True when the durable turn belongs in the job note: not the parent
 * trigger, not an already-shown opening, not already held by the ring. */
static int hush_agent_backfill_wants(const hush_agent_backfill_t *fill,
                                     const hush_thread_turn_t *turn)
{
    size_t i;

    assert(fill != NULL);
    assert(turn != NULL);
    if (strcmp(turn->id, fill->parent_id) == 0)
        return 0;
    if (fill->owner_shown && strcmp(turn->id, fill->root) == 0)
        return 0;
    for (i = 0; i < fill->nring; ++i) {
        if (strcmp(turn->id, fill->ring[i].id) == 0)
            return 0;
    }
    return 1;
}

/* Renders one durable turn under the walk's display name. */
static void hush_agent_append_backfill_turn(char *out, size_t outsz,
                                            const hush_agent_thread_walk_t *walk,
                                            const hush_thread_turn_t *turn)
{
    hush_agent_robot_t peer;
    hush_event_t ev = {0};
    const char *who = walk->robot;

    assert(out != NULL && outsz > 0);
    assert(walk != NULL && turn != NULL);
    hush_agent_copy(ev.content, sizeof(ev.content), turn->content);
    if (strcmp(turn->pubkey, walk->human_pub) == 0)
        who = walk->human;
    else if (walk->launch != NULL &&
             hush_agent_lookup_robot(&peer, walk->launch, turn->pubkey))
        who = peer.name;
    hush_agent_append_turn(out, outsz, &ev, who);
}

/* Renders durable turns for the window slots the ring cannot fill. Keeps the
 * newest fitting turns so the backfill abuts the live window. */
static void hush_agent_append_durable(char *out, size_t outsz,
                                      const hush_agent_thread_walk_t *walk,
                                      const hush_agent_backfill_t *fill)
{
    hush_thread_turn_t turns[HUSH_AGENT_THREAD_MAX];
    size_t wanted = 0;
    size_t skip;
    size_t i;
    size_t got;

    assert(out != NULL && outsz > 0);
    assert(walk != NULL && walk->root != NULL);
    assert(fill != NULL);
    if (fill->room == 0)
        return;
    got = hush_thread_read(walk->root, turns, HUSH_AGENT_THREAD_MAX);
    for (i = 0; i < got; ++i) {
        if (hush_agent_backfill_wants(fill, &turns[i]))
            wanted++;
    }
    skip = wanted > fill->room ? wanted - fill->room : 0;
    for (i = 0; i < got; ++i) {
        if (!hush_agent_backfill_wants(fill, &turns[i]))
            continue;
        if (skip > 0) {
            skip--;
            continue;
        }
        hush_agent_append_backfill_turn(out, outsz, walk, &turns[i]);
    }
}

/* Renders the window in budget order: durable backfill, live ring turns,
 * then the verbatim current message. */
static void hush_agent_render_window(char *out, size_t outsz,
                                     const hush_agent_thread_walk_t *walk,
                                     const hush_agent_backfill_t *fill,
                                     const hush_event_t *parent)
{
    assert(out != NULL && outsz > 0);
    assert(walk != NULL && fill != NULL && parent != NULL);
    hush_agent_append_durable(out, outsz, walk, fill);
    if (fill->nring > 0)
        hush_agent_walk_thread(out, outsz, fill->ring, fill->nring, walk);
    hush_agent_append_current(out, outsz, parent);
}

/* Appends the verbatim current message, replacing the note on overflow. */
static void hush_agent_append_current(char *out, size_t outsz,
                                      const hush_event_t *parent)
{
    size_t used;
    int written;

    assert(out != NULL && outsz > 0);
    assert(parent != NULL);
    used = strlen(out);
    written = snprintf(out + used, outsz - used, "\nCurrent message: %s",
                       parent->content);
    if (written < 0 || (size_t)written >= outsz - used)
        hush_agent_copy(out, outsz, parent->content);
}

/* Renders the thread: brief, opening, durable backfill, live ring turns,
 * current message. The ring is preferred; durable turns cover only the
 * window slots the ring cannot fill, so root eviction or restart degrades
 * to brief plus transcript instead of current-message-only. */
void hush_agent_fill_thread(char *out, size_t outsz, hush_store_t *store,
                            const hush_launch_t *launch, const hush_event_t *parent,
                            const hush_agent_thread_walk_t *names)
{
    char root[HUSH_EVENT_ID_HEX_LEN + 1] = {0};
    hush_event_t original = {0};
    hush_event_t events[HUSH_AGENT_THREAD_MAX] = {0};
    hush_agent_thread_walk_t walk;
    hush_agent_backfill_t fill;
    const char *human;
    size_t count;
    int owner;

    assert(out != NULL && outsz > 0);
    assert(parent != NULL && names != NULL);
    out[0] = '\0';
    if (store == NULL)
        return;
    hush_agent_event_root(root, sizeof(root), parent);
    human = hush_agent_resolve_owner(store, &original, root, parent);
    count = hush_agent_collect_thread(store, events, root, parent->id);
    assert(count <= (size_t)HUSH_AGENT_THREAD_MAX);
    walk = *names;
    walk.launch = launch;
    walk.root = root;
    walk.human_pub = human;
    owner = original.id[0] != '\0' && strcmp(original.id, parent->id) != 0;
    hush_agent_copy(out, outsz, HUSH_AGENT_THREAD_HEAD);
    hush_agent_append_brief(out, outsz, root);
    if (owner)
        hush_agent_append_turn(out, outsz, &original, "Conversation owner");
    fill.ring = events;
    fill.nring = count;
    fill.room = (size_t)HUSH_AGENT_THREAD_MAX - count;
    fill.root = root;
    fill.parent_id = parent->id;
    fill.owner_shown = owner;
    hush_agent_render_window(out, outsz, &walk, &fill, parent);
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

