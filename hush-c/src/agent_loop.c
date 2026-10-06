/* agent_loop.c: pure text helpers for the human-approved robot loop (#280).
 * Parses and strips the lead robot's "LOOP:" control line, reads a human
 * Yes/No answer, and renders the lead's loop rule. No store or job state. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

#define HUSH_AGENT_LOOP_TAG "LOOP:"
#define HUSH_AGENT_LOOP_WORD_CONTINUE "continue"
#define HUSH_AGENT_LOOP_RULE \
    " Loop: if the human asked you and your partner to keep going back and " \
    "forth (for example, take turns until someone is stumped), end your note " \
    "with one final line that is exactly \"LOOP: continue\" to keep the " \
    "exchange going, or \"LOOP: stop <reason>\" when it should end. If the " \
    "human did not ask for a repeated exchange, write no LOOP line. Hush " \
    "removes that line before posting."
#define HUSH_AGENT_LOOP_WHOLE " Whole message from "

enum {
    HUSH_AGENT_LOOP_TAG_LEN = 5,
    HUSH_AGENT_LOOP_ANSWER_MAX = 8
};

/* Reads one control line body (text after "LOOP:"); only "continue" continues. */
static hush_agent_loop_verdict_t hush_agent_loop_read_verdict(const char *body, size_t len);
/* True when the line at text[0..len) starts with the LOOP tag after blanks. */
static int hush_agent_loop_is_control(const char *line, size_t len, size_t *body_at);
/* Replaces CR, LF, and tab with spaces so the text stays on one prompt line. */
static void hush_agent_loop_flatten(char *text);

static int hush_agent_loop_is_control(const char *line, size_t len, size_t *body_at)
{
    size_t i = 0;

    assert(line != NULL && body_at != NULL);
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    if (len - i < (size_t)HUSH_AGENT_LOOP_TAG_LEN ||
        strncmp(line + i, HUSH_AGENT_LOOP_TAG, (size_t)HUSH_AGENT_LOOP_TAG_LEN) != 0)
        return 0;
    *body_at = i + (size_t)HUSH_AGENT_LOOP_TAG_LEN;
    return 1;
}

static hush_agent_loop_verdict_t hush_agent_loop_read_verdict(const char *body, size_t len)
{
    size_t i = 0;
    size_t word = strlen(HUSH_AGENT_LOOP_WORD_CONTINUE);

    assert(body != NULL);
    while (i < len && (body[i] == ' ' || body[i] == '\t'))
        i++;
    if (len - i >= word && strncmp(body + i, HUSH_AGENT_LOOP_WORD_CONTINUE, word) == 0) {
        size_t j = i + word;

        while (j < len && (body[j] == ' ' || body[j] == '\t' || body[j] == '\r' ||
                           body[j] == '.' || body[j] == '!'))
            j++;
        if (j == len)
            return HUSH_AGENT_LOOP_CONTINUE;
    }
    /* "stop <reason>" and anything garbled both end the loop. */
    return HUSH_AGENT_LOOP_STOP;
}

hush_agent_loop_verdict_t hush_agent_loop_take_control(char *text)
{
    hush_agent_loop_verdict_t verdict = HUSH_AGENT_LOOP_NONE;
    size_t len;
    size_t in = 0;
    size_t out = 0;

    assert(text != NULL);
    len = strlen(text);
    assert(len <= (size_t)HUSH_EVENT_MAX_CONTENT);
    while (in < len) {
        const char *nl = memchr(text + in, '\n', len - in);
        size_t line_len = nl != NULL ? (size_t)(nl - (text + in)) : len - in;
        size_t span = nl != NULL ? line_len + 1 : line_len;
        size_t body_at = 0;

        if (hush_agent_loop_is_control(text + in, line_len, &body_at))
            verdict = hush_agent_loop_read_verdict(text + in + body_at, line_len - body_at);
        else {
            memmove(text + out, text + in, span);
            out += span;
        }
        in += span;
    }
    text[out] = '\0';
    hush_agent_trim(text);
    return verdict;
}

hush_agent_loop_answer_t hush_agent_loop_parse_answer(const char *content)
{
    char word[HUSH_AGENT_LOOP_ANSWER_MAX + 1] = {0};
    size_t i = 0;
    size_t n = 0;

    if (content == NULL)
        return HUSH_AGENT_LOOP_ANSWER_NONE;
    while (content[i] != '\0' && isspace((unsigned char)content[i]) &&
           i < (size_t)HUSH_EVENT_MAX_CONTENT)
        i++;
    while (content[i] != '\0' && isalpha((unsigned char)content[i])) {
        if (n >= (size_t)HUSH_AGENT_LOOP_ANSWER_MAX)
            return HUSH_AGENT_LOOP_ANSWER_NONE;
        word[n++] = (char)tolower((unsigned char)content[i]);
        i++;
    }
    while (content[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT) {
        if (!isspace((unsigned char)content[i]) && strchr(".!", content[i]) == NULL)
            return HUSH_AGENT_LOOP_ANSWER_NONE;
        i++;
    }
    if (strcmp(word, "yes") == 0)
        return HUSH_AGENT_LOOP_ANSWER_YES;
    if (strcmp(word, "no") == 0)
        return HUSH_AGENT_LOOP_ANSWER_NO;
    return HUSH_AGENT_LOOP_ANSWER_NONE;
}

static void hush_agent_loop_flatten(char *text)
{
    assert(text != NULL);
    for (size_t i = 0; text[i] != '\0' && i < (size_t)HUSH_EVENT_MAX_CONTENT; ++i) {
        if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t')
            text[i] = ' ';
    }
}

void hush_agent_loop_append_lead(char *prompt, size_t promptsz,
                                 const char *human, const char *note)
{
    char flat[HUSH_EVENT_MAX_CONTENT + 1];
    size_t used;
    int n;

    assert(prompt != NULL && promptsz > 0);
    assert(note != NULL);
    hush_agent_copy(flat, sizeof(flat), note);
    hush_agent_loop_flatten(flat);
    used = strlen(prompt);
    n = snprintf(prompt + used, promptsz - used, "%s%s%s: %s",
                 HUSH_AGENT_LOOP_RULE, HUSH_AGENT_LOOP_WHOLE,
                 human != NULL && human[0] != '\0' ? human : HUSH_AGENT_HUMAN_FALLBACK,
                 flat);
    if (n < 0 || (size_t)n >= promptsz - used)
        prompt[used] = '\0';
}
