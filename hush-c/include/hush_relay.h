/* hush_relay.h: poll-based relay server and connection dispatch for Hush. */

#ifndef HUSH_RELAY_H
#define HUSH_RELAY_H

#include <stdint.h>
#include <sys/types.h>

#include "hush_status.h"

enum {
    HUSH_DEFAULT_PORT = 10555
};

/* Run the relay on given TCP port. Blocks until error or shutdown.
 * If open_ui is non-zero, open the chat UI as a standalone app window.
 * If the port is already taken and open_ui is set, reopen the UI and return OK. */
hush_status_t hush_relay_run(uint16_t port, int open_ui);

/* Ask a running poll loop to stop. Safe from HTTP handlers and signals. */
void hush_relay_request_shutdown(void);

/* Send SIGTERM to the instance that owns this port's pidfile. Idempotent. */
hush_status_t hush_relay_quit(uint16_t port);

/* Remember a forked UI or login child so Exit can stop it. pid <= 0 is ignored. */
void hush_relay_track_child(pid_t pid);

/* SIGTERM then SIGKILL tracked children. Linux also sweeps leftover --app windows. */
void hush_relay_reap_children(void);

#endif /* HUSH_RELAY_H */
