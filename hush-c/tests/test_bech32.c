/* tests/test_bech32.c: NIP-19 official npub/nsec vectors. */

#include <stdio.h>
#include <string.h>

#include "hush_bech32.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static int hex_byte(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex32(unsigned char *out, const char *hex)
{
    size_t i;
    int hi;
    int lo;

    if (strlen(hex) != 64)
        return 0;
    for (i = 0; i < 32; ++i) {
        hi = hex_byte(hex[i * 2]);
        lo = hex_byte(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

static void check_roundtrip(const char *hrp, const char *hex, const char *want)
{
    unsigned char raw[32];
    unsigned char back[32];
    char encoded[HUSH_BECH32_TEXT_MAX];
    char got_hrp[HUSH_BECH32_HRP_MAX + 1];

    expect(parse_hex32(raw, hex), "parse hex");
    expect(hush_bech32_encode(encoded, sizeof(encoded), hrp, raw) == HUSH_OK,
           "encode");
    expect(strcmp(encoded, want) == 0, want);
    expect(hush_bech32_decode(back, got_hrp, sizeof(got_hrp), want) == HUSH_OK,
           "decode");
    expect(strcmp(got_hrp, hrp) == 0, "hrp");
    expect(memcmp(back, raw, 32) == 0, "payload");
}

int main(void)
{
    unsigned char raw[32];
    char hrp[16];

    check_roundtrip(
        HUSH_BECH32_HRP_NPUB,
        "7e7e9c42a91bfef19fa929e5fda1b72e0ebc1a4c1141673e2794234d86addf4e",
        "npub10elfcs4fr0l0r8af98jlmgdh9c8tcxjvz9qkw038js35mp4dma8qzvjptg");
    check_roundtrip(
        HUSH_BECH32_HRP_NSEC,
        "67dea2ed018072d675f5415ecfaed7d2597555e202d85b3d65ea4e58d2d92ffa",
        "nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5");
    expect(hush_bech32_decode(raw, hrp, sizeof(hrp), "nsec1qq") != HUSH_OK,
           "short rejected");
    expect(hush_bech32_decode(raw, hrp, sizeof(hrp), "not-a-key") != HUSH_OK,
           "junk rejected");

    if (g_fail)
        return 1;
    printf("test_bech32 ok\n");
    return 0;
}
