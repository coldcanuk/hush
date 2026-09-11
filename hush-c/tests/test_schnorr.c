/* tests/test_schnorr.c: BIP-340 verification against the official vectors. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_schnorr.h"

enum {
    VECTOR_LINE_MAX = 1024,
    VECTOR_FIELD_MAX = 512,
    VECTOR_BYTES_MAX = 128,
    VECTOR_COUNT = 19
};

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

/* Decodes hex into out. Returns the byte count, or 0 when empty or malformed. */
static size_t hex_bytes(const char *hex, unsigned char *out, size_t outsz)
{
    size_t len = strlen(hex);
    size_t i;

    if (len == 0 || len % 2 != 0 || len / 2 > outsz)
        return 0;
    for (i = 0; i < len / 2; ++i) {
        int hi = hex_digit(hex[i * 2]);
        int lo = hex_digit(hex[i * 2 + 1]);

        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return len / 2;
}

/* Copies CSV field index (0-based) into out. 0 when the field is absent. */
static int csv_field(const char *line, size_t index, char *out, size_t outsz)
{
    size_t field = 0;
    size_t n = 0;
    size_t i = 0;

    out[0] = '\0';
    for (;;) {
        char ch = line[i];

        if (ch == ',' || ch == '\0' || ch == '\n' || ch == '\r') {
            if (field == index) {
                out[n] = '\0';
                return 1;
            }
            if (ch == '\0' || ch == '\n' || ch == '\r')
                return 0;
            field++;
            n = 0;
            i++;
            continue;
        }
        if (field == index && n + 1 < outsz)
            out[n++] = ch;
        i++;
    }
}

int main(void)
{
    FILE *fp;
    char line[VECTOR_LINE_MAX];
    size_t rows = 0;

    fp = fopen("tests/vectors/bip340_test_vectors.csv", "r");
    if (fp == NULL) {
        fprintf(stderr, "FAIL cannot open bip340 vectors\n");
        return 1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char pubkey[VECTOR_FIELD_MAX];
        char message[VECTOR_FIELD_MAX];
        char signature[VECTOR_FIELD_MAX];
        char result[VECTOR_FIELD_MAX];
        unsigned char pub[VECTOR_BYTES_MAX];
        unsigned char msg[VECTOR_BYTES_MAX];
        unsigned char sig[VECTOR_BYTES_MAX];
        size_t pub_len;
        size_t msg_len;
        size_t sig_len;
        int expected;
        int valid;

        if (line[0] == '#' || strncmp(line, "index,", 6) == 0)
            continue;
        if (!csv_field(line, 2, pubkey, sizeof(pubkey)) ||
            !csv_field(line, 4, message, sizeof(message)) ||
            !csv_field(line, 5, signature, sizeof(signature)) ||
            !csv_field(line, 6, result, sizeof(result)))
            continue;
        memset(pub, 0, sizeof(pub));
        memset(msg, 0, sizeof(msg));
        memset(sig, 0, sizeof(sig));
        pub_len = hex_bytes(pubkey, pub, sizeof(pub));
        msg_len = hex_bytes(message, msg, sizeof(msg));
        sig_len = hex_bytes(signature, sig, sizeof(sig));
        expected = strncmp(result, "TRUE", 4) == 0;
        valid = 0;
        if (pub_len == (size_t)HUSH_SCHNORR_PUBKEY_BYTES &&
            sig_len == (size_t)HUSH_SCHNORR_SIGNATURE_BYTES) {
            hush_schnorr_request_t request = {
                .pubkey = pub,
                .message = msg,
                .message_len = msg_len,
                .signature = sig
            };

            valid = hush_schnorr_verify(&request) == HUSH_OK;
        }
        if (valid != expected) {
            fprintf(stderr, "FAIL vector %zu: expected %s got %s\n", rows,
                    expected ? "TRUE" : "FALSE", valid ? "TRUE" : "FALSE");
            g_fail = 1;
        }
        rows++;
    }
    fclose(fp);
    expect(rows == (size_t)VECTOR_COUNT, "nineteen vectors");
    if (g_fail)
        return 1;
    printf("test_schnorr ok (%zu vectors)\n", rows);
    return 0;
}
