/* tests/test_event.c: real NIP-01 id computation and structural validation. */

#include <stdio.h>
#include <string.h>

#include "hush_event.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void fill_pubkey(char *out, char c)
{
    size_t i;

    for (i = 0; i < (size_t)HUSH_EVENT_PUBKEY_HEX_LEN; i++)
        out[i] = c;
    out[HUSH_EVENT_PUBKEY_HEX_LEN] = '\0';
}

int main(void)
{
    hush_event_t ev;
    char id[HUSH_EVENT_ID_HEX_LEN + 1];

    /* Argument guards. */
    expect(hush_event_compute_id(NULL, id) == HUSH_ERR_ARG, "NULL event arg");
    expect(hush_event_compute_id(&ev, NULL) == HUSH_ERR_ARG, "NULL out arg");
    memset(&ev, 0, sizeof(ev));
    expect(hush_event_compute_id(&ev, id) == HUSH_ERR_ARG,
           "empty pubkey rejected");

    /* Vector 1: minimal event, no tags, empty content. */
    memset(&ev, 0, sizeof(ev));
    fill_pubkey(ev.pubkey, 'a');
    ev.kind = 1;
    ev.created_at = 1700000000;
    ev.tag_count = 0;
    ev.content[0] = '\0';
    expect(hush_event_compute_id(&ev, id) == HUSH_OK, "minimal id ok");
    expect(strcmp(id, "57353dc6509d696afd511d4600cf178896de06197ed63326af3e99c7b5c4a4b2") == 0,
           "minimal NIP-01 id matches sha256 preimage");

    /* Vector 2: tags and content. */
    memset(&ev, 0, sizeof(ev));
    memcpy(ev.pubkey,
           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
           HUSH_EVENT_PUBKEY_HEX_LEN + 1);
    ev.kind = 1;
    ev.created_at = 1700000000;
    ev.tag_count = 2;
    memcpy(ev.tags[0][0], "t", 2);
    memcpy(ev.tags[0][1], "test", 5);
    memcpy(ev.tags[1][0], "h", 2);
    memcpy(ev.tags[1][1], "general", 8);
    memcpy(ev.content, "hello world", 12);
    expect(hush_event_compute_id(&ev, id) == HUSH_OK, "tagged id ok");
    expect(strcmp(id, "3932fb394e1df37ad341e25a7322ab6ba80be4daab6f9ee073dec3b4950306f4") == 0,
           "tagged NIP-01 id matches sha256 preimage");

    /* Vector 3: content needs JSON escaping (quote, newline). */
    memset(&ev, 0, sizeof(ev));
    fill_pubkey(ev.pubkey, 'b');
    ev.kind = 30023;
    ev.created_at = 1700000001;
    ev.tag_count = 0;
    memcpy(ev.content, "say \"hi\"\nnewline", 17);
    expect(hush_event_compute_id(&ev, id) == HUSH_OK, "escaped id ok");
    expect(strcmp(id, "2757d0512713cb129c58d0817118942db211569befff27328f5441e55dd4141d") == 0,
           "escaped NIP-01 id matches sha256 preimage");

    /* Determinism: same input yields the same id twice. */
    {
        char again[HUSH_EVENT_ID_HEX_LEN + 1];

        expect(hush_event_compute_id(&ev, again) == HUSH_OK, "recompute ok");
        expect(strcmp(id, again) == 0, "id computation is deterministic");
    }

    /* validate(): well-formed computed event passes. */
    memcpy(ev.id, id, HUSH_EVENT_ID_HEX_LEN + 1);
    expect(hush_event_validate(&ev) == HUSH_OK, "computed event validates");
    ev.id[HUSH_EVENT_ID_HEX_LEN - 1] = '\0';
    expect(hush_event_validate(&ev) == HUSH_ERR_ARG, "short id rejected");
    memcpy(ev.id, id, HUSH_EVENT_ID_HEX_LEN + 1);
    ev.kind = 65536;
    expect(hush_event_validate(&ev) == HUSH_ERR_ARG, "oversize kind rejected");

    if (g_fail)
        return 1;
    printf("ok\n");
    return 0;
}
