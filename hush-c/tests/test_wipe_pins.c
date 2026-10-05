/* tests/test_wipe_pins.c: pin claimed secure wipes (Gauge #257 F2).
 * Links with -Wl,--wrap=hush_secure_zero and -Wl,--wrap=BN_clear_free. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/bn.h>

#include "hush_bech32.h"
#include "hush_http.h"
#include "hush_http_internal.h"
#include "hush_identity.h"
#include "hush_launch.h"
#include "hush_mem.h"

enum {
    HUSH_WIPE_LOG_MAX = 64,
    HUSH_WIPE_TRIM = 160,
    HUSH_WIPE_BECH32 = 96,
    HUSH_WIPE_ID = 385,
    HUSH_WIPE_HTTP_SECRET = 128
};

static int g_fail;
static size_t g_wipe_log[HUSH_WIPE_LOG_MAX];
static size_t g_wipe_n;
static int g_wipe_on;
static int g_bn_clear_free_calls;

void __real_hush_secure_zero(void *buf, size_t n);
void __real_BN_clear_free(BIGNUM *a);

void __wrap_hush_secure_zero(void *buf, size_t n)
{
    if (g_wipe_on && g_wipe_n < (size_t)HUSH_WIPE_LOG_MAX)
        g_wipe_log[g_wipe_n++] = n;
    __real_hush_secure_zero(buf, n);
}

void __wrap_BN_clear_free(BIGNUM *a)
{
    g_bn_clear_free_calls++;
    __real_BN_clear_free(a);
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static void wipe_reset(void)
{
    g_wipe_n = 0;
    memset(g_wipe_log, 0, sizeof(g_wipe_log));
}

static size_t wipe_count(size_t nbytes)
{
    size_t i;
    size_t n = 0;

    for (i = 0; i < g_wipe_n; i++) {
        if (g_wipe_log[i] == nbytes)
            n++;
    }
    return n;
}

static int id_all_zero(const hush_identity_t *id)
{
    size_t i;
    const unsigned char *p = (const unsigned char *)id;

    for (i = 0; i < sizeof(*id); i++) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

static void drain_fd(int fd)
{
    char buf[4096];
    ssize_t n;

    n = read(fd, buf, sizeof(buf));
    (void)n;
}

/* M3: trimmed[] wipe on every import return after the copy. */
static void test_trimmed_wipe(void)
{
    hush_identity_t id;

    wipe_reset();
    g_wipe_on = 1;
    expect(hush_identity_import(&id, "xyz") != HUSH_OK, "garbage import fails");
    g_wipe_on = 0;
    expect(wipe_count((size_t)HUSH_WIPE_TRIM) >= 1, "trimmed wipe on parse fail");
    expect(id_all_zero(&id), "failed import clears id");
}

/* M4: failed hex/derive import leaves no leftover id bytes. */
static void test_failed_import_clears(void)
{
    hush_identity_t id;
    static const char bad_tail[] =
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffg";
    static const char zero_hex[] =
        "0000000000000000000000000000000000000000000000000000000000000000";

    expect(hush_identity_import(&id, bad_tail) != HUSH_OK, "bad hex fails");
    expect(id_all_zero(&id), "bad hex leaves all-zero id");
    expect(hush_identity_import(&id, zero_hex) != HUSH_OK, "zero hex derive fails");
    expect(id_all_zero(&id), "zero hex leaves all-zero id");
}

/* M7: secret BIGNUM released with BN_clear_free. */
static void test_bn_clear_free(void)
{
    hush_identity_t id;
    char nsec[HUSH_IDENTITY_NSEC_MAX];

    g_bn_clear_free_calls = 0;
    expect(hush_identity_generate(&id) == HUSH_OK, "generate ok");
    expect(g_bn_clear_free_calls >= 1, "BN_clear_free on generate");
    snprintf(nsec, sizeof(nsec), "%s", id.nsec);
    g_bn_clear_free_calls = 0;
    expect(hush_identity_import(&id, nsec) == HUSH_OK, "reimport ok");
    expect(g_bn_clear_free_calls >= 1, "BN_clear_free on import derive");
}

/* M8–M10: bech32 checksum / encode_finish / decode_finish wipes. */
static void test_bech32_wipes(void)
{
    unsigned char data[HUSH_BECH32_DATA_LEN];
    char text[HUSH_BECH32_TEXT_MAX];
    char hrp[HUSH_BECH32_HRP_MAX + 1];
    char badsum[HUSH_BECH32_TEXT_MAX];
    size_t n;

    memset(data, 0x11, sizeof(data));
    wipe_reset();
    g_wipe_on = 1;
    expect(hush_bech32_encode(text, sizeof(text), HUSH_BECH32_HRP_NSEC, data) ==
               HUSH_OK,
           "encode ok");
    g_wipe_on = 0;
    expect(wipe_count((size_t)HUSH_WIPE_BECH32) >= 2,
           "encode_finish wipes five and values");

    wipe_reset();
    g_wipe_on = 1;
    expect(hush_bech32_decode(data, hrp, sizeof(hrp), text) == HUSH_OK,
           "decode ok");
    g_wipe_on = 0;
    expect(wipe_count((size_t)HUSH_WIPE_BECH32) >= 2,
           "checksum_ok and decode_finish wipe");

    snprintf(badsum, sizeof(badsum), "%s", text);
    n = strlen(badsum);
    expect(n > 1, "bech32 text length");
    badsum[n - 1] = (badsum[n - 1] == 'q') ? 'p' : 'q';
    wipe_reset();
    g_wipe_on = 1;
    expect(hush_bech32_decode(data, hrp, sizeof(hrp), badsum) != HUSH_OK,
           "bad checksum fails");
    g_wipe_on = 0;
    expect(wipe_count((size_t)HUSH_WIPE_BECH32) >= 2,
           "bad checksum still wipes values and data");
}

/* M11, M12: api_identity secret[] wiped on missing field and after import. */
static void test_http_import_wipes(void)
{
    hush_launch_t launch;
    hush_identity_t gen;
    char body[256];
    int sv[2];
    int wrote;

    hush_launch_init(&launch);
    hush_http_set_launch(&launch);
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    wipe_reset();
    g_wipe_on = 1;
    (void)hush_http_serve_identity(sv[0], "{\"action\":\"import\"}");
    g_wipe_on = 0;
    drain_fd(sv[1]);
    expect(wipe_count((size_t)HUSH_WIPE_HTTP_SECRET) >= 1,
           "missing-field secret wipe");

    expect(hush_identity_generate(&gen) == HUSH_OK, "http gen");
    wrote = snprintf(body, sizeof(body),
                     "{\"action\":\"import\",\"nsec\":\"%s\"}", gen.nsec);
    expect(wrote > 0 && (size_t)wrote < sizeof(body), "import body fits");
    wipe_reset();
    g_wipe_on = 1;
    (void)hush_http_serve_identity(sv[0], body);
    g_wipe_on = 0;
    drain_fd(sv[1]);
    expect(wipe_count((size_t)HUSH_WIPE_HTTP_SECRET) >= 1,
           "post-import secret wipe");
    expect(wipe_count((size_t)HUSH_WIPE_TRIM) >= 1, "import still wipes trimmed");

    close(sv[0]);
    close(sv[1]);
    hush_http_set_launch(NULL);
}

int main(void)
{
    expect(sizeof(hush_identity_t) == (size_t)HUSH_WIPE_ID, "id size 385");
    test_trimmed_wipe();
    test_failed_import_clears();
    test_bn_clear_free();
    test_bech32_wipes();
    test_http_import_wipes();
    if (g_fail)
        return 1;
    printf("test_wipe_pins ok\n");
    return 0;
}
