/* tests/test_proto.c: REQ sub-id and wire format smoke. */

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
    char buf[256];

    expect(hush_proto_parse_line("[\"REQ\",\"sub1\",{\"kinds\":[1]}]", &msg) == HUSH_OK,
           "parse REQ");
    expect(msg.type == HUSH_MSG_REQ, "type REQ");
    expect(strcmp(msg.sub_id, "sub1") == 0, "sub_id");
    expect(msg.nfilters == 1, "nfilters");
    expect(msg.filters[0].kinds_len == 1, "kinds_len");
    expect(msg.filters[0].kinds[0] == 1, "kind 1");

    expect(hush_proto_format_eose("sub1", buf, sizeof(buf), NULL) == HUSH_OK, "eose");
    expect(strcmp(buf, "[\"EOSE\",\"sub1\"]\n") == 0, "eose text");
    expect(hush_proto_format_ok("ab", 1, "", buf, sizeof(buf), NULL) == HUSH_OK, "ok");
    expect(strcmp(buf, "[\"OK\",\"ab\",true,\"\"]\n") == 0, "ok text");

    if (g_fail)
        return 1;
    printf("test_proto ok\n");
    return 0;
}
