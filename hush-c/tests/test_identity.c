/* tests/test_identity.c: NIP-19 nsec import plus generate smoke. */

#include <stdio.h>
#include <string.h>

#include "hush_identity.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    hush_identity_t id;
    hush_identity_t again;

    expect(hush_identity_import(
               &id,
               "nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5") ==
               HUSH_OK,
           "import nsec");
    expect(strcmp(id.npub,
                  "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg") ==
               0,
           "known npub");
    expect(strcmp(id.pubkey_hex,
                  "7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e") ==
               0,
           "known hex");
    expect(hush_identity_import(&again, id.nsec) == HUSH_OK, "reimport");
    expect(strcmp(again.npub, id.npub) == 0, "same npub");
    hush_identity_clear(&id);
    expect(id.nsec[0] == '\0', "cleared");
    expect(hush_identity_generate(&id) == HUSH_OK, "generate");
    expect(strncmp(id.nsec, "nsec1", 5) == 0, "nsec prefix");
    expect(strncmp(id.npub, "npub1", 5) == 0, "npub prefix");
    expect(hush_identity_import(&again, "not-a-key") == HUSH_ERR_PARSE,
           "junk");
    hush_identity_clear(&id);
    hush_identity_clear(&again);
    if (g_fail)
        return 1;
    printf("test_identity ok\n");
    return 0;
}
