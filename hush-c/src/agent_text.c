/* agent_text.c: note text munging for the agent core. Owns mention
 * rewriting, npub expansion, alias mapping, reply scrub, and the small
 * shared text utilities (whitespace/npub scanning, line snipping). */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

#define HUSH_AGENT_HOFF_TURN "your turn"
#define HUSH_AGENT_HOFF_NEXT "take the next turn"
#define HUSH_AGENT_HOFF_HERE "take it from here"
#define HUSH_AGENT_HOFF_CONT "continue the thread"
#define HUSH_AGENT_HOFF_DONE "riddle answered"
#define HUSH_AGENT_HOFF_GEN "generate a new riddle"
#define HUSH_AGENT_AT_NPUB "@npub1"
#define HUSH_AGENT_NOSTR_HEAD "nostr:"
#define HUSH_AGENT_NOSTR_NPUB "nostr:npub1"
#define HUSH_AGENT_NPUB_HEAD "npub1"

/* True when ch is a bech32/npub body character [0-9a-z]. Pure. */
static int hush_agent_is_npub_char(char ch);
/* Length of a nostr:npub1… or npub1… token starting at src[i], else 0. */
static size_t hush_agent_npub_span(const char *src, size_t i);
/* Writes one space at o when room remains. Returns the next index. */
static size_t hush_agent_put_gap(char *out, size_t o, size_t cap);
/* Strips self mentions, echoed ask, handoff phrases; last drops peers. */
static void hush_agent_scrub_reply(hush_agent_job_t *job);
/* Removes nostr:<own npub> and @OwnName from job->out. */
static void hush_agent_drop_self(hush_agent_job_t *job);
/* Removes sentences that contain a long substring of job->ask. */
static void hush_agent_drop_echo(hush_agent_job_t *job);
/* Removes known handoff phrases from job->out. */
static void hush_agent_drop_handoff(char *text);
/* Removes remaining nostr:npub tokens. Used when the robot is last. */
static void hush_agent_drop_npubs(char *text, size_t textsz);
/* Collapses whitespace and trailing junk punctuation. */
static void hush_agent_tidy_reply(char *text);
/* Cuts needle (case-insensitive) plus following junk from text. */
static void hush_agent_cut_ci(char *text, const char *needle);
/* Replaces @npub1 with nostr:npub1 in place. text is a writable C string. */
static void hush_agent_rewrite_at_npub(char *text, size_t textsz);
/* Expands truncated nostr:npub1 tokens to the unique roster npub. */
static void hush_agent_expand_npubs(char *text, size_t textsz,
                                    const hush_launch_t *launch);
/* Replaces @Name with nostr:<npub> for roster display names, longest first. */
static void hush_agent_rewrite_at_names(char *text, size_t textsz,
                                        const hush_launch_t *launch);
/* Fills out with Payne then enabled agents. Returns the count. */
static size_t hush_agent_list_aliases(const hush_launch_t *launch,
                                      hush_agent_alias_t *out, size_t maxn);
/* Sorts aliases longest-name-first. n is the live count. */
static void hush_agent_sort_aliases(hush_agent_alias_t *aliases, size_t n);
/* Writes the unique full npub for tok (exact or prefix). Returns 0 if none. */
static int hush_agent_unique_npub(const hush_launch_t *launch,
                                  const char *tok, char *out, size_t outsz);
/* Copies src into dst, mapping @Name from set to nostr:<npub>. */
static void hush_agent_emit_at_names(char *dst, size_t dstsz, const char *src,
                                     const hush_agent_alias_set_t *set);
/* True when a[0..n) equals b[0..n) ignoring ASCII case. Pure. */
static int hush_agent_is_same_ascii(const char *a, const char *b, size_t n);
/* True when ch cannot continue a display name. Pure. */
static int hush_agent_is_name_end(char ch);
/* True when tok is npub or a unique prefix of npub (min NPUB_MIN). Pure. */
static int hush_agent_npub_prefix_hit(const char *tok, const char *npub);
/* Writes nostr:<npub> at dst[o]. Returns the next index, or cap on overflow. */
static size_t hush_agent_put_full_npub(char *dst, size_t o, size_t cap,
                                       const char *npub);
/* Index of the longest @Name match at src, or set->naliases when none. */
static size_t hush_agent_alias_at(const char *src,
                                  const hush_agent_alias_set_t *set);

int hush_agent_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int hush_agent_is_npub_char(char ch)
{
    if (ch >= '0' && ch <= '9')
        return 1;
    if (ch >= 'a' && ch <= 'z')
        return 1;
    return 0;
}

static size_t hush_agent_npub_span(const char *src, size_t i)
{
    size_t n;

    assert(src != NULL);
    if (strncmp(src + i, HUSH_AGENT_NOSTR_NPUB,
                (size_t)HUSH_AGENT_NOSTR_NPUB_LEN) == 0)
        n = (size_t)HUSH_AGENT_NOSTR_NPUB_LEN;
    else if (strncmp(src + i, HUSH_AGENT_NPUB_HEAD,
                     (size_t)HUSH_AGENT_NPUB_HEAD_LEN) == 0) {
        if (i > 0 && hush_agent_is_npub_char(src[i - 1]))
            return 0;
        n = (size_t)HUSH_AGENT_NPUB_HEAD_LEN;
    } else {
        return 0;
    }
    while (src[i + n] != '\0' && hush_agent_is_npub_char(src[i + n]) &&
           i + n < (size_t)HUSH_EVENT_MAX_CONTENT)
        n++;
    return n;
}

static size_t hush_agent_put_gap(char *out, size_t o, size_t cap)
{
    assert(out != NULL);
    if (o >= cap)
        return o;
    out[o] = ' ';
    return o + 1;
}

void hush_agent_snip_line(char *out, size_t outsz, const char *src)
{
    size_t i;
    size_t o;
    size_t hard;
    size_t soft;
    size_t span;
    int gap;

    assert(out != NULL);
    assert(outsz > 0);
    if (src == NULL)
        src = "";
    hard = outsz - 1;
    soft = hard;
    if (soft > (size_t)HUSH_AGENT_SNIP_MAX)
        soft = (size_t)HUSH_AGENT_SNIP_MAX;
    o = 0;
    gap = 0;
    for (i = 0; src[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        if (hush_agent_is_space(src[i])) {
            gap = 1;
            continue;
        }
        span = hush_agent_npub_span(src, i);
        if (span == 0 && o >= soft)
            break;
        if (gap && o > 0)
            o = hush_agent_put_gap(out, o, hard);
        if (span > 0) {
            if (o + span > hard)
                break;
            memcpy(out + o, src + i, span);
            o += span;
            i += span - 1;
            gap = 0;
            continue;
        }
        if (o >= hard)
            break;
        out[o] = src[i];
        o++;
        gap = 0;
    }
    out[o] = '\0';
}

hush_status_t hush_agent_complete_snippet(char *text, size_t capacity)
{
    assert(text != NULL && capacity > 0);
    size_t len = strlen(text);
    for (size_t i = 0; i < (size_t)HUSH_JSON_UTF8_MAX; ++i) {
        size_t characters = 0;
        hush_status_t status = hush_json_count_chars(&characters, text, capacity);
        if (status == HUSH_OK) return HUSH_OK;
        if (status != HUSH_ERR_PARSE || len == 0) return status;
        text[--len] = '\0';
    }
    return HUSH_ERR_PARSE;
}

static int hush_agent_npub_prefix_hit(const char *tok, const char *npub)
{
    size_t n;
    size_t m;

    if (tok == NULL || npub == NULL || tok[0] == '\0' || npub[0] == '\0')
        return 0;
    if (strcmp(tok, npub) == 0)
        return 1;
    n = strlen(tok);
    m = strlen(npub);
    if (n < (size_t)HUSH_AGENT_NPUB_MIN)
        return 0;
    if (n >= m)
        return 0;
    return strncmp(tok, npub, n) == 0;
}

static size_t hush_agent_list_aliases(const hush_launch_t *launch,
                                      hush_agent_alias_t *out, size_t maxn)
{
    size_t n = 0;
    size_t i;

    assert(out != NULL);
    if (launch == NULL || maxn == 0)
        return 0;
    if (launch->has_vibe && launch->payne.npub[0] != '\0' && n < maxn) {
        out[n].name = hush_launch_payne_name(launch);
        out[n].npub = launch->payne.npub;
        n++;
    }
    for (i = 0; i < launch->roster.nagents && n < maxn; i++) {
        const hush_roster_agent_t *agent = &launch->roster.agents[i];

        if (!agent->enabled || agent->id.npub[0] == '\0')
            continue;
        out[n].name = agent->name;
        out[n].npub = agent->id.npub;
        n++;
    }
    return n;
}

static void hush_agent_sort_aliases(hush_agent_alias_t *aliases, size_t n)
{
    size_t i;
    size_t j;

    assert(aliases != NULL || n == 0);
    for (i = 1; i < n; i++) {
        hush_agent_alias_t hold = aliases[i];

        j = i;
        while (j > 0 && strlen(aliases[j - 1].name) < strlen(hold.name)) {
            aliases[j] = aliases[j - 1];
            j--;
        }
        aliases[j] = hold;
    }
}

static int hush_agent_unique_npub(const hush_launch_t *launch,
                                  const char *tok, char *out, size_t outsz)
{
    hush_agent_alias_t aliases[HUSH_ROSTER_AGENTS_MAX + 1];
    size_t n;
    size_t i;
    size_t hits = 0;
    const char *hit = NULL;

    assert(out != NULL);
    assert(outsz > 0);
    out[0] = '\0';
    if (launch == NULL || tok == NULL || tok[0] == '\0')
        return 0;
    n = hush_agent_list_aliases(launch, aliases, HUSH_ROSTER_AGENTS_MAX + 1);
    for (i = 0; i < n; i++) {
        if (!hush_agent_npub_prefix_hit(tok, aliases[i].npub))
            continue;
        hits++;
        hit = aliases[i].npub;
        if (hits > 1)
            return 0;
    }
    if (hits != 1)
        return 0;
    hush_agent_copy(out, outsz, hit);
    return 1;
}

static size_t hush_agent_put_full_npub(char *dst, size_t o, size_t cap,
                                       const char *npub)
{
    size_t nlen;

    assert(dst != NULL);
    assert(npub != NULL);
    nlen = strlen(npub);
    if (o + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN + nlen >= cap)
        return cap;
    memcpy(dst + o, HUSH_AGENT_NOSTR_HEAD, (size_t)HUSH_AGENT_NOSTR_HEAD_LEN);
    o += (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
    memcpy(dst + o, npub, nlen);
    return o + nlen;
}

static int hush_agent_is_same_ascii(const char *a, const char *b, size_t n)
{
    size_t i;

    assert(a != NULL);
    assert(b != NULL);
    for (i = 0; i < n; i++) {
        unsigned char ca;
        unsigned char cb;

        if (a[i] == '\0' || b[i] == '\0')
            return 0;
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int hush_agent_is_name_end(char ch)
{
    if (ch == '\0')
        return 1;
    if (ch >= '0' && ch <= '9')
        return 0;
    if (ch >= 'A' && ch <= 'Z')
        return 0;
    if (ch >= 'a' && ch <= 'z')
        return 0;
    return 1;
}

static size_t hush_agent_alias_at(const char *src,
                                  const hush_agent_alias_set_t *set)
{
    size_t a;

    assert(src != NULL);
    assert(set != NULL);
    if (src[0] != '@')
        return set->naliases;
    for (a = 0; a < set->naliases; a++) {
        const char *nm = set->aliases[a].name;
        size_t nlen;

        if (nm == NULL || nm[0] == '\0')
            continue;
        nlen = strlen(nm);
        if (!hush_agent_is_same_ascii(src + 1, nm, nlen))
            continue;
        if (!hush_agent_is_name_end(src[1 + nlen]))
            continue;
        return a;
    }
    return set->naliases;
}

static void hush_agent_rewrite_at_npub(char *text, size_t textsz)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        if (strncmp(text + i, HUSH_AGENT_AT_NPUB,
                    (size_t)HUSH_AGENT_AT_NPUB_LEN) == 0) {
            if (o + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN >= sizeof(scratch))
                break;
            memcpy(scratch + o, HUSH_AGENT_NOSTR_HEAD,
                   (size_t)HUSH_AGENT_NOSTR_HEAD_LEN);
            o += (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
            i += 1;
            continue;
        }
        scratch[o++] = text[i++];
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_expand_npubs(char *text, size_t textsz,
                                    const hush_launch_t *launch)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    char full[HUSH_IDENTITY_NPUB_MAX];
    char tok[HUSH_IDENTITY_NPUB_MAX];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    if (launch == NULL)
        return;
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);
        size_t tlen;
        size_t next;

        if (span < (size_t)HUSH_AGENT_NOSTR_NPUB_LEN ||
            strncmp(text + i, HUSH_AGENT_NOSTR_HEAD,
                    (size_t)HUSH_AGENT_NOSTR_HEAD_LEN) != 0) {
            scratch[o++] = text[i++];
            continue;
        }
        tlen = span - (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
        if (tlen >= sizeof(tok))
            tlen = sizeof(tok) - 1;
        memcpy(tok, text + i + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN, tlen);
        tok[tlen] = '\0';
        if (!hush_agent_unique_npub(launch, tok, full, sizeof(full))) {
            hush_agent_robot_t bot;

            if (hush_agent_lookup_robot(&bot, launch, tok) &&
                o + span < sizeof(scratch)) {
                memcpy(scratch + o, text + i, span);
                o += span;
                i += span;
                continue;
            }
            i += span;
            continue;
        }
        next = hush_agent_put_full_npub(scratch, o, sizeof(scratch), full);
        if (next >= sizeof(scratch))
            break;
        o = next;
        i += span;
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_emit_at_names(char *dst, size_t dstsz, const char *src,
                                     const hush_agent_alias_set_t *set)
{
    size_t i = 0;
    size_t o = 0;

    assert(dst != NULL);
    assert(dstsz > 0);
    assert(src != NULL);
    assert(set != NULL);
    while (src[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < dstsz) {
        size_t hit = hush_agent_alias_at(src + i, set);
        size_t next;

        if (hit >= set->naliases) {
            dst[o++] = src[i++];
            continue;
        }
        next = hush_agent_put_full_npub(dst, o, dstsz, set->aliases[hit].npub);
        if (next >= dstsz)
            break;
        o = next;
        i += 1 + strlen(set->aliases[hit].name);
    }
    dst[o] = '\0';
}

static void hush_agent_rewrite_at_names(char *text, size_t textsz,
                                        const hush_launch_t *launch)
{
    hush_agent_alias_t aliases[HUSH_ROSTER_AGENTS_MAX + 1];
    hush_agent_alias_set_t set;
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];

    assert(text != NULL);
    if (launch == NULL)
        return;
    set.aliases = aliases;
    set.naliases = hush_agent_list_aliases(launch, aliases,
                                           HUSH_ROSTER_AGENTS_MAX + 1);
    hush_agent_sort_aliases(aliases, set.naliases);
    hush_agent_emit_at_names(scratch, sizeof(scratch), text, &set);
    hush_agent_copy(text, textsz, scratch);
}

void hush_agent_humanize_ask(char *text, size_t textsz,
                                    const hush_launch_t *launch,
                                    const char *self_hex)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    char tok[HUSH_IDENTITY_NPUB_MAX];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    if (launch == NULL)
        return;
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);
        size_t tlen;
        const char *name = NULL;

        if (span < (size_t)HUSH_AGENT_NOSTR_NPUB_LEN ||
            strncmp(text + i, HUSH_AGENT_NOSTR_HEAD,
                    (size_t)HUSH_AGENT_NOSTR_HEAD_LEN) != 0) {
            scratch[o++] = text[i++];
            continue;
        }
        tlen = span - (size_t)HUSH_AGENT_NOSTR_HEAD_LEN;
        if (tlen >= sizeof(tok))
            tlen = sizeof(tok) - 1;
        memcpy(tok, text + i + (size_t)HUSH_AGENT_NOSTR_HEAD_LEN, tlen);
        tok[tlen] = '\0';
        i += span;
        {
            hush_agent_robot_t bot;

            if (hush_agent_lookup_robot(&bot, launch, tok)) {
                if (self_hex != NULL && self_hex[0] != '\0' &&
                    bot.hex != NULL && strcmp(bot.hex, self_hex) == 0)
                    continue; /* drop the acting robot's own mention */
                name = bot.name;
            } else if (launch->logged_in && hush_agent_is_human(launch, tok)) {
                name = launch->roster.profile.first_name[0] != '\0'
                    ? launch->roster.profile.first_name
                    : HUSH_AGENT_HUMAN_FALLBACK;
            }
        }
        if (name == NULL || name[0] == '\0')
            continue; /* drop unknown npub */
        if (o + 2 + strlen(name) >= sizeof(scratch))
            break;
        scratch[o++] = '@';
        memcpy(scratch + o, name, strlen(name));
        o += strlen(name);
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_cut_ci(char *text, const char *needle)
{
    size_t nlen;
    size_t i;

    assert(text != NULL);
    assert(needle != NULL);
    nlen = strlen(needle);
    if (nlen == 0)
        return;
    for (i = 0; text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        size_t k;

        if (!hush_agent_is_same_ascii(text + i, needle, nlen))
            continue;
        k = i + nlen;
        while (text[k] != '\0' && text[k] != '.' && text[k] != '!' &&
               text[k] != '?' && text[k] != '\n')
            k++;
        if (text[k] != '\0')
            k++;
        memmove(text + i, text + k, strlen(text + k) + 1);
        return;
    }
}

static void hush_agent_drop_handoff(char *text)
{
    int n;

    assert(text != NULL);
    for (n = 0; n < 8; n++) {
        char before[HUSH_EVENT_MAX_CONTENT + 1];

        hush_agent_copy(before, sizeof(before), text);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_TURN);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_NEXT);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_HERE);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_CONT);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_DONE);
        hush_agent_cut_ci(text, HUSH_AGENT_HOFF_GEN);
        if (strcmp(before, text) == 0)
            return;
    }
}

static void hush_agent_drop_npubs(char *text, size_t textsz)
{
    char scratch[HUSH_EVENT_MAX_CONTENT + 1];
    size_t i = 0;
    size_t o = 0;

    assert(text != NULL);
    assert(textsz > 0);
    while (text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT &&
           o + 1 < sizeof(scratch)) {
        size_t span = hush_agent_npub_span(text, i);

        if (span > 0) {
            i += span;
            continue;
        }
        scratch[o++] = text[i++];
    }
    scratch[o] = '\0';
    hush_agent_copy(text, textsz, scratch);
}

static void hush_agent_drop_self(hush_agent_job_t *job)
{
    hush_agent_robot_t bot;
    char key[HUSH_IDENTITY_NPUB_MAX + 8];
    char *hit;

    assert(job != NULL);
    if (job->launch == NULL)
        return;
    if (!hush_agent_lookup_robot(&bot, job->launch, job->robot_pub))
        return;
    if (bot.npub != NULL && bot.npub[0] != '\0') {
        (void)snprintf(key, sizeof(key), "%s%s", HUSH_AGENT_NOSTR_HEAD,
                       bot.npub);
        while ((hit = strstr(job->out, key)) != NULL)
            memmove(hit, hit + strlen(key), strlen(hit + strlen(key)) + 1);
    }
    if (job->robot_name[0] != '\0') {
        char token[HUSH_ROSTER_NAME_MAX + 2];

        (void)snprintf(token, sizeof(token), "@%s", job->robot_name);
        hush_agent_cut_ci(job->out, token);
    }
}

static void hush_agent_drop_echo(hush_agent_job_t *job)
{
    char snip[HUSH_AGENT_SNIP_MAX + HUSH_IDENTITY_NPUB_MAX + 1];
    char needle[HUSH_AGENT_SNIP_MAX + 1];
    char *hit;
    char *a;
    char *b;
    size_t i;
    size_t o = 0;

    assert(job != NULL);
    hush_agent_snip_line(snip, sizeof(snip), job->ask);
    for (i = 0; snip[i] != '\0' && o + 1 < sizeof(needle); ) {
        size_t span = hush_agent_npub_span(snip, i);

        if (span > 0) {
            i += span;
            continue;
        }
        needle[o++] = snip[i++];
    }
    needle[o] = '\0';
    if (o > (size_t)HUSH_AGENT_ECHO_MIN)
        needle[HUSH_AGENT_ECHO_MIN] = '\0';
    if (strlen(needle) < 12)
        return;
    hit = strstr(job->out, needle);
    if (hit == NULL)
        return;
    a = hit;
    while (a > job->out && a[-1] != '.' && a[-1] != '!' && a[-1] != '?')
        a--;
    b = hit;
    while (*b != '\0' && *b != '.' && *b != '!' && *b != '?')
        b++;
    if (*b != '\0')
        b++;
    if (a == job->out && *b == '\0')
        return;
    memmove(a, b, strlen(b) + 1);
}

static void hush_agent_tidy_reply(char *text)
{
    size_t i;
    size_t o = 0;
    int gap = 0;

    assert(text != NULL);
    for (i = 0; text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; i++) {
        if (text[i] == ' ' || text[i] == '\n' || text[i] == '\r') {
            gap = 1;
            continue;
        }
        if (gap && o > 0 && text[i] != ',' && text[i] != ';' &&
            text[i] != '.')
            text[o++] = ' ';
        gap = 0;
        text[o++] = text[i];
    }
    text[o] = '\0';
    while (o > 0) {
        char c = text[o - 1];

        if (c == ';' || c == ',' || c == ':' || c == ' ') {
            text[--o] = '\0';
            continue;
        }
        break;
    }
}

static void hush_agent_scrub_reply(hush_agent_job_t *job)
{
    assert(job != NULL);
    hush_agent_drop_self(job);
    hush_agent_drop_echo(job);
    hush_agent_drop_handoff(job->out);
    if (job->last)
        hush_agent_drop_npubs(job->out, sizeof(job->out));
    hush_agent_tidy_reply(job->out);
}

void hush_agent_rewrite_mentions(hush_agent_job_t *job)
{
    assert(job != NULL);
    if (job->kind == HUSH_AGENT_KIND_FIXUP ||
        job->kind == HUSH_AGENT_KIND_ELECT)
        return;
    hush_agent_rewrite_at_npub(job->out, sizeof(job->out));
    if (job->launch == NULL)
        return;
    hush_agent_expand_npubs(job->out, sizeof(job->out), job->launch);
    hush_agent_rewrite_at_names(job->out, sizeof(job->out), job->launch);
    hush_agent_scrub_reply(job);
}

