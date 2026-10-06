/* tests/test_loop.c: #280 loop control-line strip, Yes/No parse, and
 * loop notices that never count as robot turns. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "hush_agent.h"
#include "hush_agent_internal.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

/* Strips text in a scratch copy; checks the verdict and what stays. */
static void expect_take(const char *in, hush_agent_loop_verdict_t want,
                        const char *left, const char *msg)
{
    char text[HUSH_EVENT_MAX_CONTENT + 1];
    hush_agent_loop_verdict_t got;

    hush_agent_copy(text, sizeof(text), in);
    got = hush_agent_loop_take_control(text);
    expect(got == want, msg);
    expect(strcmp(text, left) == 0, msg);
}

int main(void)
{
    char prompt[256] = "Base.";

    expect_take("Riddle one.\nLOOP: continue", HUSH_AGENT_LOOP_CONTINUE,
                "Riddle one.", "continue strips and continues");
    expect_take("Riddle one.\nLOOP: continue.\n", HUSH_AGENT_LOOP_CONTINUE,
                "Riddle one.", "trailing dot and newline still continue");
    expect_take("Stumped.\nLOOP: stop he got it", HUSH_AGENT_LOOP_STOP,
                "Stumped.", "stop with a reason stops");
    expect_take("Hmm.\nLOOP: maybe", HUSH_AGENT_LOOP_STOP,
                "Hmm.", "garbled control line stops");
    expect_take("Hmm.\nLOOP: continue please", HUSH_AGENT_LOOP_STOP,
                "Hmm.", "continue with extra words is garbled");
    expect_take("Plain answer.", HUSH_AGENT_LOOP_NONE,
                "Plain answer.", "no control line leaves text alone");
    expect_take("LOOP: continue\nBody.\nLOOP: stop done", HUSH_AGENT_LOOP_STOP,
                "Body.", "every control line goes; the last one wins");
    expect_take("I said LOOP: continue inline.", HUSH_AGENT_LOOP_NONE,
                "I said LOOP: continue inline.", "only line-leading tags count");

    expect(hush_agent_loop_parse_answer("Yes") == HUSH_AGENT_LOOP_ANSWER_YES, "Yes");
    expect(hush_agent_loop_parse_answer("  yes! ") == HUSH_AGENT_LOOP_ANSWER_YES, "yes!");
    expect(hush_agent_loop_parse_answer("YES.") == HUSH_AGENT_LOOP_ANSWER_YES, "YES.");
    expect(hush_agent_loop_parse_answer("No") == HUSH_AGENT_LOOP_ANSWER_NO, "No");
    expect(hush_agent_loop_parse_answer("no.") == HUSH_AGENT_LOOP_ANSWER_NO, "no.");
    expect(hush_agent_loop_parse_answer("yes please") == HUSH_AGENT_LOOP_ANSWER_NONE,
           "yes please is not a bare Yes");
    expect(hush_agent_loop_parse_answer("nope") == HUSH_AGENT_LOOP_ANSWER_NONE, "nope");
    expect(hush_agent_loop_parse_answer("") == HUSH_AGENT_LOOP_ANSWER_NONE, "empty");
    expect(hush_agent_loop_parse_answer(NULL) == HUSH_AGENT_LOOP_ANSWER_NONE, "NULL");

    expect(!hush_agent_is_work_note(HUSH_AGENT_LOOP_ASK_LINE), "prompt is not a turn");
    expect(!hush_agent_is_work_note(HUSH_AGENT_LOOP_LIMIT_LINE), "limit is not a turn");
    expect(!hush_agent_is_work_note(HUSH_AGENT_LOOP_STOPPED_LINE), "stopped is not a turn");
    expect(hush_agent_is_work_note("lead turn 1."), "a robot answer is a turn");

    hush_agent_loop_append_lead(prompt, sizeof(prompt), "Chuck", "line one\nline two");
    expect(strstr(prompt, "\n") == NULL, "lead block stays on one line");
    expect(strncmp(prompt, "Base.", 5) == 0, "lead block appends");

    if (g_fail)
        return 1;
    printf("loop ok\n");
    return 0;
}
