/* tests/test_turn.c: ICE JSON and vibe-independent turn status. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hush_turn.h"

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
    hush_turn_t turn;
    char json[HUSH_TURN_JSON_MAX];
    size_t n = 0;
    char *dir;

    hush_turn_init(&turn);
    expect(turn.compiled_in == 1 || turn.compiled_in == 0, "compiled flag");
    expect(turn.listen_port == (uint16_t)HUSH_TURN_PORT_PRIV ||
               turn.listen_port == (uint16_t)HUSH_TURN_PORT_USER,
           "port");
    expect(hush_turn_format_ice(&turn, json, sizeof(json), &n) == HUSH_OK,
           "ice off");
    expect(strstr(json, "iceServers") != NULL, "ice servers");
    expect(strstr(json, "stun.l.google.com") != NULL, "fallback stun");
    expect(hush_turn_format_status(&turn, json, sizeof(json), &n) == HUSH_OK,
           "status");
    expect(strstr(json, "\"mode\":\"off\"") != NULL, "mode off");
    expect(hush_turn_set_public_host(&turn, "203.0.113.8") == HUSH_OK, "host");
    expect(strcmp(turn.public_host, "203.0.113.8") == 0, "host set");
    expect(hush_turn_set_public_host(&turn, "bad\"host") == HUSH_ERR_PARSE,
           "reject inject host");
    expect(strcmp(turn.public_host, "203.0.113.8") == 0, "host unchanged");
    expect(hush_turn_set_public_host(&turn, "") == HUSH_OK, "host empty");
    expect(strcmp(turn.public_host, "127.0.0.1") == 0, "host local");
    expect(hush_turn_set_public_host(&turn, "203.0.113.8") == HUSH_OK, "host restore");

    dir = "/tmp/hush-turn-test";
    (void)snprintf(turn.state_dir, sizeof(turn.state_dir), "%s", dir);
    (void)snprintf(turn.conf_path, sizeof(turn.conf_path),
                   "%s/turnserver.conf", dir);
    memcpy(turn.password, "testpass000000000000000000000000", 33);
    {
        char conf[HUSH_TURN_CONF_MAX];

        /* #353: daemon mode leaves log-file and pidfile to the unit's flags. */
        expect(hush_turn_format_conf(&turn, HUSH_TURN_MODE_DAEMON, conf, sizeof(conf)) ==
                   HUSH_OK, "daemon conf formats");
        expect(strstr(conf, "user=hush:testpass") != NULL, "daemon conf has the credential");
        expect(strstr(conf, "log-file") == NULL, "daemon conf names no log-file");
        expect(strstr(conf, "pidfile") == NULL, "daemon conf names no pidfile");
        expect(strstr(conf, "external-ip=203.0.113.8\n") != NULL, "daemon conf external ip");
        expect(hush_turn_format_conf(&turn, HUSH_TURN_MODE_CHILD, conf, sizeof(conf)) ==
                   HUSH_OK, "child conf formats");
        expect(strstr(conf, "log-file=/tmp/hush-turn-test/turnserver.log\n") != NULL,
               "child conf keeps its log-file");
        expect(strstr(conf, "pidfile=/tmp/hush-turn-test/turnserver.pid\n") != NULL,
               "child conf keeps its pidfile");
        expect(hush_turn_format_conf(&turn, HUSH_TURN_MODE_CHILD, conf, 64) == HUSH_ERR_FULL,
               "short conf buffer refused");
    }
    if (turn.compiled_in) {
        expect(hush_turn_enable(&turn, HUSH_TURN_MODE_CHILD) == HUSH_OK ||
                   hush_turn_enable(&turn, HUSH_TURN_MODE_CHILD) ==
                       HUSH_ERR_NOT_FOUND,
               "enable child or missing binary");
        if (turn.enabled) {
            expect(hush_turn_format_ice(&turn, json, sizeof(json),
                                        &n) == HUSH_OK,
                   "ice on");
            expect(strstr(json, "turn:203.0.113.8") != NULL, "turn url");
            expect(strstr(json, "testpass") != NULL, "cred");
            expect(access(turn.conf_path, R_OK) == 0, "conf written");
            expect(hush_turn_disable(&turn) == HUSH_OK, "disable");
        }
    }
    expect(hush_turn_enable(&turn, HUSH_TURN_MODE_OFF) == HUSH_ERR_ARG,
           "bad mode");
    if (g_fail)
        return 1;
    printf("test_turn ok\n");
    return 0;
}
