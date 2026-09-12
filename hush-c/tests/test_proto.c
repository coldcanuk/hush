/* tests/test_proto.c: wire parser/serializer fidelity. */

#include <stdio.h>
#include <string.h>
#include "hush_proto.h"

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
    hush_client_msg_t msg;
    char buf[1024];

    expect(hush_proto_parse_line("[\"REQ\",\"sub1\",{\"kinds\":[1]}]", &msg) == HUSH_OK,
           "parse REQ");
    expect(msg.type == HUSH_MSG_REQ, "type REQ");
    expect(strcmp(msg.sub_id, "sub1") == 0, "sub_id");
    expect(msg.nfilters == 1, "nfilters");
    expect(msg.filters[0].kinds_len == 1, "kinds_len");
    expect(msg.filters[0].kinds[0] == 1, "kind 1");

    {
        const char *req =
            "[\"REQ\",\"s2\",{\"kinds\":[1,7],\"ids\":[\"aa\",\"bb\"],"
            "\"authors\":[\"cc\",\"dd\"],\"since\":10,\"until\":20,"
            "\"#h\":[\"general\",\"research\",\"ops\"]}]";

        expect(hush_proto_parse_line(req, &msg) == HUSH_OK, "parse full REQ");
        expect(msg.filters[0].kinds_len == 2, "two kinds");
        expect(msg.filters[0].ids_len == 2, "two ids");
        expect(strcmp(msg.filters[0].ids[1], "bb") == 0, "second id");
        expect(msg.filters[0].authors_len == 2, "two authors");
        expect(strcmp(msg.filters[0].authors[0], "cc") == 0, "first author");
        expect(msg.filters[0].since == 10 && msg.filters[0].until == 20,
               "since and until");
        expect(msg.filters[0].tag_count == 1, "one tag filter");
        expect(strcmp(msg.filters[0].tag_keys[0], "h") == 0, "h key");
        expect(msg.filters[0].tag_vals_len[0] == 3, "three h values");
        expect(strcmp(msg.filters[0].tag_vals[0][2], "ops") == 0, "third h");
    }

    {
        const char *ev =
            "[\"EVENT\",\"s3\",{\"id\":\"1111111111111111111111111111111111111111111111111111111111111111\","
            "\"pubkey\":\"2222222222222222222222222222222222222222222222222222222222222222\","
            "\"kind\":1,\"created_at\":1720000123,"
            "\"content\":\"hi \\\"you\\\"\\nnext\\tend\","
            "\"tags\":[[\"e\",\"3333333333333333333333333333333333333333333333333333333333333333\"],"
            "[\"p\",\"4444444444444444444444444444444444444444444444444444444444444444\"],[\"t\",\"x\",\"y\"]]}]";

        expect(hush_proto_parse_line(ev, &msg) == HUSH_OK, "parse EVENT");
        expect(msg.type == HUSH_MSG_EVENT, "type EVENT");
        expect(msg.event.created_at == 1720000123, "created_at kept");
        expect(msg.event.tag_count == 3, "three tags");
        expect(strcmp(msg.event.tags[0][0], "e") == 0, "tag e key");
        expect(strcmp(msg.event.tags[1][1],
                      "4444444444444444444444444444444444444444444444444444444444444444") == 0,
               "tag p value");
        expect(strcmp(msg.event.tags[2][2], "y") == 0, "third tag element");
        expect(strcmp(msg.event.content, "hi \"you\"\nnext\tend") == 0,
               "content decoded");
    }

    expect(hush_proto_parse_line("[\"COUNT\",\"s4\",{\"kinds\":[1]}]", &msg) == HUSH_OK,
           "parse COUNT");
    expect(msg.type == HUSH_MSG_COUNT, "type COUNT");

    {
        hush_event_t out;
        hush_client_msg_t back;
        size_t written = 0;

        memset(&out, 0, sizeof(out));
        memset(out.id, 'a', sizeof(out.id) - 1);
        memset(out.pubkey, 'b', sizeof(out.pubkey) - 1);
        out.kind = 1;
        out.created_at = 1720000999;
        memcpy(out.content, "tab\tquote\"slash\\", 17);
        out.tag_count = 2;
        memcpy(out.tags[0][0], "e", 2);
        memset(out.tags[0][1], 'c', 8);
        memcpy(out.tags[1][0], "p", 2);
        memset(out.tags[1][1], 'd', 8);
        expect(hush_proto_format_event("sub", &out, buf, sizeof(buf),
                                       &written) == HUSH_OK,
               "format event");
        expect(hush_proto_parse_line(buf, &back) == HUSH_OK, "round-trip parse");
        expect(back.type == HUSH_MSG_EVENT, "round-trip type");
        expect(strcmp(back.event.content, out.content) == 0,
               "content round-trip");
        expect(back.event.created_at == out.created_at,
               "created_at round-trip");
        expect(back.event.tag_count == 2, "tags round-trip");
        expect(strcmp(back.event.tags[1][1], out.tags[1][1]) == 0,
               "tag value round-trip");
    }

    expect(hush_proto_format_eose("sub1", buf, sizeof(buf), NULL) == HUSH_OK,
           "eose");
    expect(strcmp(buf, "[\"EOSE\",\"sub1\"]\n") == 0, "eose text");
    expect(hush_proto_format_ok("ab", 1, "", buf, sizeof(buf), NULL) == HUSH_OK,
           "ok");
    expect(strcmp(buf, "[\"OK\",\"ab\",true,\"\"]\n") == 0, "ok text");

    {
        const char *auth =
            "[\"AUTH\",{\"id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
            "\"pubkey\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
            "\"kind\":22242,\"created_at\":1720000999,\"content\":\"\","
            "\"tags\":[[\"relay\",\"ws://localhost:1234\"],[\"challenge\",\"abc\"]]}]";

        expect(hush_proto_parse_line(auth, &msg) == HUSH_OK, "parse AUTH");
        expect(msg.type == HUSH_MSG_AUTH, "type AUTH");
        expect(msg.event.kind == 22242, "AUTH kind");
        expect(strcmp(msg.event.tags[0][0], "relay") == 0, "AUTH relay tag");
        expect(strcmp(msg.event.tags[1][1], "abc") == 0, "AUTH challenge");
    }

    expect(hush_proto_parse_line("[\"AUTH\",\"deadbeef\"]", &msg) == HUSH_OK,
           "parse AUTH server form");
    expect(msg.type == HUSH_MSG_AUTH && msg.event.id[0] == '\0',
           "server form ignored");

    expect(hush_proto_parse_line("[\"JOIN\",\"0123456789abcdef\"]", &msg) == HUSH_OK,
           "parse JOIN");
    expect(msg.type == HUSH_MSG_JOIN, "type JOIN");
    expect(strcmp(msg.join_token, "0123456789abcdef") == 0, "join token");

    expect(hush_proto_format_auth("deadbeef", buf, sizeof(buf), NULL) == HUSH_OK,
           "format auth");
    expect(strcmp(buf, "[\"AUTH\",\"deadbeef\"]\n") == 0, "auth text");
    expect(hush_proto_format_closed("s9", "auth-required: no", buf, sizeof(buf),
                                    NULL) == HUSH_OK,
           "format closed");
    expect(strcmp(buf, "[\"CLOSED\",\"s9\",\"auth-required: no\"]\n") == 0,
           "closed text");
    expect(hush_proto_format_notice("joined", buf, sizeof(buf), NULL) == HUSH_OK,
           "format notice");
    expect(strcmp(buf, "[\"NOTICE\",\"joined\"]\n") == 0, "notice text");

    if (g_fail)
        return 1;
    printf("test_proto ok\n");
    return 0;
}
