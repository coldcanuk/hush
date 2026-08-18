/* hush_bech32.c: owns NIP-19 bech32 (not bech32m) for 32-byte npub/nsec. */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "hush_bech32.h"

enum {
    HUSH_BECH32_CHARSET_LEN = 32,
    HUSH_BECH32_CHECKSUM_LEN = 6,
    HUSH_BECH32_GEN0 = 0x3b6a57b2,
    HUSH_BECH32_GEN1 = 0x26508e6d,
    HUSH_BECH32_GEN2 = 0x1ea119fa,
    HUSH_BECH32_GEN3 = 0x3d4233dd,
    HUSH_BECH32_GEN4 = 0x2a1462b3,
    HUSH_BECH32_CHK_MASK = 0x1ffffff,
    HUSH_BECH32_BITS_8 = 8,
    HUSH_BECH32_BITS_5 = 5,
    HUSH_BECH32_PAD_BITS = 4,
    HUSH_BECH32_EXPAND_MAX = 32,
    HUSH_BECH32_VALUES_MAX = 96
};

#define HUSH_BECH32_CHARSET "qpzry9x8gf2tvdw0s3jn54khce6mua7l"

/* Mixes one 5-bit value into the bech32 polymod checksum. */
static uint32_t hush_bech32_polymod_step(uint32_t chk, unsigned v);

/* Writes HRP high bits, a zero separator, then HRP low bits. */
static size_t hush_bech32_expand_hrp(unsigned char *out, size_t outsz,
                                     const char *hrp);

/* Converts 8-bit bytes to 5-bit groups, padding the leftover bits. */
static size_t hush_bech32_to_five(unsigned char *out, size_t outsz,
                                  const unsigned char *in, size_t inlen);

/* Converts 5-bit groups to 8-bit bytes. Rejects leftover non-zero pad bits. */
static size_t hush_bech32_to_eight(unsigned char *out, size_t outsz,
                                   const unsigned char *in, size_t inlen);

/* Writes the 6 checksum 5-bit values for values[0..n). */
static void hush_bech32_checksum(unsigned char *out6,
                                 const unsigned char *values, size_t n);

/* True when polymod of expand(hrp) + data + checksum equals 1. */
static int hush_bech32_checksum_ok(const char *hrp,
                                   const unsigned char *data, size_t n);

/* Maps a charset character to 0..31, or -1. */
static int hush_bech32_value(char c);

hush_status_t hush_bech32_encode(char *out, size_t outsz,
                                 const char *hrp, const unsigned char *data32)
{
    unsigned char values[HUSH_BECH32_VALUES_MAX];
    unsigned char five[HUSH_BECH32_VALUES_MAX];
    unsigned char sum[HUSH_BECH32_CHECKSUM_LEN];
    size_t hlen;
    size_t nfive;
    size_t nval;
    size_t i;
    size_t need;

    if (out == NULL || hrp == NULL || data32 == NULL)
        return HUSH_ERR_ARG;
    if (hrp[0] == '\0' || strlen(hrp) > (size_t)HUSH_BECH32_HRP_MAX)
        return HUSH_ERR_ARG;

    nfive = hush_bech32_to_five(five, sizeof(five),
                                data32, HUSH_BECH32_DATA_LEN);
    hlen = hush_bech32_expand_hrp(values, sizeof(values), hrp);
    if (nfive == 0 || hlen == 0)
        return HUSH_ERR_ARG;
    if (hlen + nfive > sizeof(values))
        return HUSH_ERR_ARG;
    memcpy(values + hlen, five, nfive);
    nval = hlen + nfive;
    hush_bech32_checksum(sum, values, nval);

    need = strlen(hrp) + 1 + nfive + HUSH_BECH32_CHECKSUM_LEN + 1;
    if (outsz < need)
        return HUSH_ERR_ARG;
    memcpy(out, hrp, strlen(hrp));
    out[strlen(hrp)] = '1';
    for (i = 0; i < nfive; ++i)
        out[strlen(hrp) + 1 + i] = HUSH_BECH32_CHARSET[five[i]];
    for (i = 0; i < HUSH_BECH32_CHECKSUM_LEN; ++i)
        out[strlen(hrp) + 1 + nfive + i] = HUSH_BECH32_CHARSET[sum[i]];
    out[need - 1] = '\0';
    return HUSH_OK;
}

hush_status_t hush_bech32_decode(unsigned char *out32,
                                 char *hrp_out, size_t hrp_outsz,
                                 const char *text)
{
    unsigned char data[HUSH_BECH32_VALUES_MAX];
    const char *sep;
    size_t hrplen;
    size_t payload;
    size_t i;
    size_t n8;
    int v;

    if (out32 == NULL || hrp_out == NULL || text == NULL)
        return HUSH_ERR_ARG;
    sep = strrchr(text, '1');
    if (sep == NULL || sep == text)
        return HUSH_ERR_PARSE;
    hrplen = (size_t)(sep - text);
    if (hrplen == 0 || hrplen > (size_t)HUSH_BECH32_HRP_MAX)
        return HUSH_ERR_PARSE;
    if (hrplen + 1 >= hrp_outsz)
        return HUSH_ERR_ARG;
    payload = strlen(sep + 1);
    if (payload <= (size_t)HUSH_BECH32_CHECKSUM_LEN)
        return HUSH_ERR_PARSE;
    if (payload - HUSH_BECH32_CHECKSUM_LEN > sizeof(data))
        return HUSH_ERR_PARSE;
    for (i = 0; i < payload; ++i) {
        v = hush_bech32_value(sep[1 + i]);
        if (v < 0)
            return HUSH_ERR_PARSE;
        data[i] = (unsigned char)v;
    }
    memcpy(hrp_out, text, hrplen);
    hrp_out[hrplen] = '\0';
    if (!hush_bech32_checksum_ok(hrp_out, data, payload))
        return HUSH_ERR_PARSE;
    n8 = hush_bech32_to_eight(out32, HUSH_BECH32_DATA_LEN, data,
                              payload - HUSH_BECH32_CHECKSUM_LEN);
    if (n8 != (size_t)HUSH_BECH32_DATA_LEN)
        return HUSH_ERR_PARSE;
    return HUSH_OK;
}

static uint32_t hush_bech32_polymod_step(uint32_t chk, unsigned v)
{
    uint32_t top;

    top = chk >> 25;
    chk = ((chk & HUSH_BECH32_CHK_MASK) << 5) ^ (uint32_t)v;
    if ((top & 1u) != 0)
        chk ^= (uint32_t)HUSH_BECH32_GEN0;
    if ((top & 2u) != 0)
        chk ^= (uint32_t)HUSH_BECH32_GEN1;
    if ((top & 4u) != 0)
        chk ^= (uint32_t)HUSH_BECH32_GEN2;
    if ((top & 8u) != 0)
        chk ^= (uint32_t)HUSH_BECH32_GEN3;
    if ((top & 16u) != 0)
        chk ^= (uint32_t)HUSH_BECH32_GEN4;
    return chk;
}

static size_t hush_bech32_expand_hrp(unsigned char *out, size_t outsz,
                                     const char *hrp)
{
    size_t n;
    size_t i;

    assert(out != NULL);
    assert(hrp != NULL);
    n = strlen(hrp);
    if (n * 2 + 1 > outsz)
        return 0;
    for (i = 0; i < n; ++i)
        out[i] = (unsigned char)((unsigned char)hrp[i] >> 5);
    out[n] = 0;
    for (i = 0; i < n; ++i)
        out[n + 1 + i] = (unsigned char)((unsigned char)hrp[i] & 31u);
    return n * 2 + 1;
}

static size_t hush_bech32_to_five(unsigned char *out, size_t outsz,
                                  const unsigned char *in, size_t inlen)
{
    size_t i;
    size_t n = 0;
    unsigned acc = 0;
    unsigned bits = 0;

    assert(out != NULL);
    assert(in != NULL);
    for (i = 0; i < inlen; ++i) {
        acc = (acc << HUSH_BECH32_BITS_8) | in[i];
        bits += HUSH_BECH32_BITS_8;
        while (bits >= (unsigned)HUSH_BECH32_BITS_5) {
            bits -= HUSH_BECH32_BITS_5;
            if (n >= outsz)
                return 0;
            out[n++] = (unsigned char)((acc >> bits) & 31u);
        }
    }
    if (bits != 0) {
        if (n >= outsz)
            return 0;
        out[n++] = (unsigned char)((acc << (HUSH_BECH32_BITS_5 - bits)) & 31u);
    }
    return n;
}

static size_t hush_bech32_to_eight(unsigned char *out, size_t outsz,
                                   const unsigned char *in, size_t inlen)
{
    size_t i;
    size_t n = 0;
    unsigned acc = 0;
    unsigned bits = 0;

    assert(out != NULL);
    assert(in != NULL);
    for (i = 0; i < inlen; ++i) {
        acc = (acc << HUSH_BECH32_BITS_5) | in[i];
        bits += HUSH_BECH32_BITS_5;
        if (bits >= (unsigned)HUSH_BECH32_BITS_8) {
            bits -= HUSH_BECH32_BITS_8;
            if (n >= outsz)
                return 0;
            out[n++] = (unsigned char)((acc >> bits) & 0xffu);
        }
    }
    if (bits >= (unsigned)HUSH_BECH32_BITS_8)
        return 0;
    if (bits != 0 && ((acc << (HUSH_BECH32_BITS_8 - bits)) & 0xffu) != 0)
        return 0;
    (void)HUSH_BECH32_PAD_BITS;
    return n;
}

static void hush_bech32_checksum(unsigned char *out6,
                                 const unsigned char *values, size_t n)
{
    uint32_t chk = 1;
    size_t i;

    assert(out6 != NULL);
    assert(values != NULL);
    for (i = 0; i < n; ++i)
        chk = hush_bech32_polymod_step(chk, values[i]);
    for (i = 0; i < HUSH_BECH32_CHECKSUM_LEN; ++i)
        chk = hush_bech32_polymod_step(chk, 0);
    chk ^= 1u;
    for (i = 0; i < HUSH_BECH32_CHECKSUM_LEN; ++i) {
        unsigned shift = 5u * (5u - (unsigned)i);

        out6[i] = (unsigned char)((chk >> shift) & 31u);
    }
}

static int hush_bech32_checksum_ok(const char *hrp,
                                   const unsigned char *data, size_t n)
{
    unsigned char values[HUSH_BECH32_VALUES_MAX];
    size_t hlen;
    size_t i;
    uint32_t chk = 1;

    assert(hrp != NULL);
    assert(data != NULL);
    hlen = hush_bech32_expand_hrp(values, sizeof(values), hrp);
    if (hlen == 0 || hlen + n > sizeof(values))
        return 0;
    memcpy(values + hlen, data, n);
    for (i = 0; i < hlen + n; ++i)
        chk = hush_bech32_polymod_step(chk, values[i]);
    return chk == 1u;
}

static int hush_bech32_value(char c)
{
    const char *p;

    if (c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');
    p = strchr(HUSH_BECH32_CHARSET, c);
    if (p == NULL)
        return -1;
    return (int)(p - HUSH_BECH32_CHARSET);
}
