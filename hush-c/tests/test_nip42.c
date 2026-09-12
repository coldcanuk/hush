/* tests/test_nip42.c: NIP-42 AUTH-event validation against pinned fixtures. */

#include <stdio.h>
#include <string.h>

#include "hush_nip42.h"
#include "hush_proto.h"

/* Pinned AUTH events signed by tests/sign_bip340.py (independent pure-Python
 * BIP-340 implementation) with seckey 1, created_at 1720000999, challenge
 * "unit-test-challenge". The pubkey is the x-coordinate of G. The C verifier
 * is independently anchored to the official BIP-340 vector CSV in
 * tests/test_schnorr.c. */
#define HUSH_TEST_AUTH_LINE \
    "[\"AUTH\",{\"id\":\"a9c538d755f6631c809690d8ddf7fe9836db59eda65178b13b4d5f3213e0fec1\"," \
    "\"pubkey\":\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\"," \
    "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\"," \
    "\"tags\":[[\"relay\",\"ws://localhost:7777\"],[\"challenge\",\"unit-test-challenge\"]]," \
    "\"sig\":\"0fd5192746c0af167df63f57f8fb44932eb597c69c3b923de5e2d59aa04ef11e" \
    "9f1d2cc27ac9b6b68b898971f5a282f2b2a730a0357e8c568f7f53e321adc550\"}]"

#define HUSH_TEST_AUTH_LOOPBACK \
    "[\"AUTH\",{\"id\":\"3957d849def48679b7df8082632b14bfdcacf4b51c28c2017d93b668711a8e65\"," \
    "\"pubkey\":\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\"," \
    "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\"," \
    "\"tags\":[[\"relay\",\"ws://127.0.0.1:1234\"],[\"challenge\",\"unit-test-challenge\"]]," \
    "\"sig\":\"4b0db51187f4ad4618fc99466531eae4ac3a0bc50ed973bde58159fba9178d7" \
    "8f338cdae7c2997ee4350bcccac935bc5113cf17b64995da8e5c2255553b0c591\"}]"

#define HUSH_TEST_AUTH_NO_RELAY \
    "[\"AUTH\",{\"id\":\"9657c014ab5f856185687932896573ed1ecb02ab89b8f167eb631dd22e7d92f7\"," \
    "\"pubkey\":\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\"," \
    "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\"," \
    "\"tags\":[[\"challenge\",\"unit-test-challenge\"]]," \
    "\"sig\":\"ff1d347ac8ab7c06cb786954c595d2559c7740ad0cd53a765d1b39e75c3fe51e" \
    "f17ca3059493928d8278e9d2a97a91f978a1e8d4aead5feae39fd82ba51b37f1\"}]"

#define HUSH_TEST_AUTH_EVIL_RELAY \
    "[\"AUTH\",{\"id\":\"41fe8ceda5b4b96b270c7afff4596a15103b0c10f4a3cbd6026ecc4cd7fd7c98\"," \
    "\"pubkey\":\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\"," \
    "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\"," \
    "\"tags\":[[\"relay\",\"wss://evil.example\"],[\"challenge\",\"unit-test-challenge\"]]," \
    "\"sig\":\"a9ed2ea4ebce60ed4be0642c9a8737dc56b6a39b72ada0881bc78bf1fc010bfe" \
    "7ba8fbabddf78c262b1a8761287504091787271da245ba0ffcd8565de7d474f4\"}]"

#define HUSH_TEST_AUTH_NO_CHALLENGE \
    "[\"AUTH\",{\"id\":\"d0f9f8c6abfa227346605918ad0a79c136379bfe9de95e4f771ee1759c45f217\"," \
    "\"pubkey\":\"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\"," \
    "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\"," \
    "\"tags\":[[\"relay\",\"ws://localhost\"]]," \
    "\"sig\":\"89c04b1ab2c1bd2f7500dc37d96369e39a945ec7fd03044dc131761eced662c3" \
    "af37d4ed57bc8f2436c445ca389c605a4e8e76d3f11d1309b2a0138e8b023719\"}]"

enum {
    HUSH_TEST_NOW = 1720001099,
    HUSH_TEST_NOW_BOUNDARY = 1720001599,
    HUSH_TEST_NOW_STALE = 1720001600,
    HUSH_TEST_NOW_STALE_PAST = 1720000398
};

static int g_fail;

static void expect(int cond, const char *msg);

/* Parses the pinned AUTH line into out. */
static hush_status_t parse_fixture(hush_event_t *out, const char *line);

/* Validates ev against the loopback bind, writing the denial text into
 * reason (HUSH_EVENT_REASON_MAX bytes). */
static hush_status_t check_loopback(hush_event_t *ev, time_t now,
                                    const char *challenge, char *reason);

int main(void)
{
    char reason[HUSH_EVENT_REASON_MAX];
    hush_event_t ev;
    hush_nip42_request_t empty = {0};
    hush_status_t st;

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LINE) == HUSH_OK, "parse fixture");
    expect(check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                          reason) == HUSH_OK,
           "valid AUTH accepted");
    expect(check_loopback(&ev, (time_t)HUSH_TEST_NOW_BOUNDARY,
                          "unit-test-challenge", reason) == HUSH_OK,
           "window boundary accepted");
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW_STALE,
                        "unit-test-challenge", reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "stale") != NULL,
           "stale future rejected");
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW_STALE_PAST,
                        "unit-test-challenge", reason);
    expect(st == HUSH_ERR_DENIED, "stale past rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LINE) == HUSH_OK, "reparse kind");
    ev.kind = 1;
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "kind") != NULL,
           "wrong kind rejected");

    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED, "mutated kind stays rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LINE) == HUSH_OK,
           "reparse challenge");
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "other-challenge", reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "challenge") != NULL,
           "wrong challenge rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_NO_CHALLENGE) == HUSH_OK,
           "parse missing challenge");
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED, "missing challenge tag rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_NO_RELAY) == HUSH_OK,
           "parse missing relay");
    expect(check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                          reason) == HUSH_OK,
           "absent relay tag accepted");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_EVIL_RELAY) == HUSH_OK,
           "parse evil relay");
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "relay") != NULL,
           "relay mismatch rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LOOPBACK) == HUSH_OK,
           "parse loopback relay");
    expect(check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                          reason) == HUSH_OK,
           "loopback relay accepted");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LINE) == HUSH_OK,
           "reparse content");
    ev.content[0] = 'X';
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "id") != NULL,
           "tampered content rejected");

    expect(parse_fixture(&ev, HUSH_TEST_AUTH_LINE) == HUSH_OK, "reparse sig");
    ev.sig[127] = 'f';
    st = check_loopback(&ev, (time_t)HUSH_TEST_NOW, "unit-test-challenge",
                        reason);
    expect(st == HUSH_ERR_DENIED && strstr(reason, "signature") != NULL,
           "tampered signature rejected");

    expect(hush_nip42_validate(NULL, reason, sizeof(reason)) == HUSH_ERR_ARG,
           "NULL request rejected");
    expect(hush_nip42_validate(&empty, reason, sizeof(reason)) == HUSH_ERR_ARG,
           "empty request rejected");

    expect(hush_nip42_relay_host_ok(NULL, "127.0.0.1") == 0, "NULL url false");
    expect(hush_nip42_relay_host_ok("", "127.0.0.1") == 0, "empty url false");
    expect(hush_nip42_relay_host_ok("ws://localhost:7777", "127.0.0.1") == 1,
           "localhost with port");
    expect(hush_nip42_relay_host_ok("ws://127.0.0.1", "127.0.0.1") == 1,
           "ipv4 loopback");
    expect(hush_nip42_relay_host_ok("ws://::1/", "") == 1, "ipv6 loopback");
    expect(hush_nip42_relay_host_ok("ws://[::1]:8080/path", "") == 1,
           "bracketed ipv6");
    expect(hush_nip42_relay_host_ok("wss://evil.example", "127.0.0.1") == 0,
           "foreign host false");
    expect(hush_nip42_relay_host_ok("ws://10.0.0.5", "10.0.0.5") == 1,
           "bind address match");
    expect(hush_nip42_relay_host_ok("ws://10.0.0.5", "10.0.0.6") == 0,
           "bind address mismatch");

    if (g_fail)
        return 1;
    printf("test_nip42 ok\n");
    return 0;
}

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

static hush_status_t parse_fixture(hush_event_t *out, const char *line)
{
    hush_client_msg_t msg;

    if (hush_proto_parse_line(line, &msg) != HUSH_OK)
        return HUSH_ERR_PARSE;
    if (msg.type != HUSH_MSG_AUTH)
        return HUSH_ERR_PARSE;
    *out = msg.event;
    return HUSH_OK;
}

static hush_status_t check_loopback(hush_event_t *ev, time_t now,
                                    const char *challenge, char *reason)
{
    hush_nip42_request_t request = {.event = ev, .now = now,
                                    .challenge = challenge,
                                    .bind_addr = "127.0.0.1"};

    return hush_nip42_validate(&request, reason, HUSH_EVENT_REASON_MAX);
}
