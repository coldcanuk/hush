/* hush_turn.h: optional coturn STUN/TURN manager for Hush. */

#ifndef HUSH_TURN_H
#define HUSH_TURN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "hush_status.h"

enum {
    HUSH_TURN_PORT_PRIV = 3478,
    HUSH_TURN_PORT_USER = 13478,
    HUSH_TURN_RELAY_MIN = 49152,
    HUSH_TURN_RELAY_MAX = 49251,
    HUSH_TURN_HOST_MAX = 128,
    HUSH_TURN_REALM_MAX = 64,
    HUSH_TURN_USER_MAX = 32,
    HUSH_TURN_PASS_MAX = 64,
    HUSH_TURN_PATH_MAX = 384,
    HUSH_TURN_JSON_MAX = 1536,
    HUSH_TURN_SECRET_BYTES = 16
};

#define HUSH_TURN_REALM_DEFAULT "hush.local"
#define HUSH_TURN_USER_DEFAULT "hush"

typedef enum {
    HUSH_TURN_MODE_OFF = 0,
    HUSH_TURN_MODE_CHILD = 1,
    HUSH_TURN_MODE_DAEMON = 2
} hush_turn_mode_t;

typedef struct {
    int compiled_in;
    int enabled;
    int running;
    int have_binary;
    int have_unit;
    int need_root;
    hush_turn_mode_t mode;
    pid_t child_pid;
    uint16_t listen_port;
    char public_host[HUSH_TURN_HOST_MAX];
    char realm[HUSH_TURN_REALM_MAX];
    char username[HUSH_TURN_USER_MAX];
    char password[HUSH_TURN_PASS_MAX];
    char binary[HUSH_TURN_PATH_MAX];
    char state_dir[HUSH_TURN_PATH_MAX];
    char conf_path[HUSH_TURN_PATH_MAX];
} hush_turn_t;

/* Zeros state, resolves paths, looks for turnserver. Safe on NULL. */
void hush_turn_init(hush_turn_t *turn);

/* Writes conf and starts child or systemd unit. */
hush_status_t hush_turn_enable(hush_turn_t *turn, hush_turn_mode_t mode);

/* Stops child or systemd unit. */
hush_status_t hush_turn_disable(hush_turn_t *turn);

/* Copies a public ICE host. Empty restores 127.0.0.1. */
hush_status_t hush_turn_set_public_host(hush_turn_t *turn, const char *host);

/* Reaps a dead child and refreshes running. */
void hush_turn_refresh(hush_turn_t *turn);

/* Writes iceServers JSON. */
hush_status_t hush_turn_format_ice(const hush_turn_t *turn,
                                   char *out, size_t outsz, size_t *out_len);

/* Writes turn status JSON. */
hush_status_t hush_turn_format_status(const hush_turn_t *turn,
                                      char *out, size_t outsz, size_t *out_len);

/* 1 when Whisper (or HUSH_WHISPER) is available for agent voice. */
int hush_turn_whisper_available(void);

/* Stops a child we own. */
void hush_turn_shutdown(hush_turn_t *turn);

#endif /* HUSH_TURN_H */
