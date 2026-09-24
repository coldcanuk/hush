/* hush_relay.h: poll-based relay server and connection dispatch for Hush. */

#ifndef HUSH_RELAY_H
#define HUSH_RELAY_H

#include <stdint.h>
#include <sys/types.h>

#include "hush_status.h"

enum {
    HUSH_DEFAULT_PORT = 10555
};

/* Run the relay on given TCP port, bound to bind_addr (NULL or empty means
 * 127.0.0.1). Blocks until error or shutdown. If open_ui is non-zero, open the
 * chat UI as a standalone app window. If the port is already taken and open_ui
 * is set, reopen the UI and return OK. */
hush_status_t hush_relay_run(uint16_t port, const char *bind_addr, int open_ui);

/* Ask a running poll loop to stop. Safe from HTTP handlers and signals. */
void hush_relay_request_shutdown(void);

/* Stops the relay that owns this port's pidfile: verifies the owner is a live
 * hush-relay (Linux exe check; always refused off Linux), SIGTERMs it, and
 * waits up to HUSH_QUIT_OWNER_TRIES x HUSH_QUIT_OWNER_MS. Port 0 means the
 * default port. Succeeds only when the owner is confirmed gone; NOT_FOUND
 * when no pidfile or live owner exists; PARSE on an unreadable pidfile;
 * DENIED when the pid is not a verified relay; IO when the owner survives. */
hush_status_t hush_relay_quit(uint16_t port);

/* Remember a forked UI or login child so Exit can stop it. pid <= 0 is ignored. */
void hush_relay_track_child(pid_t pid);

/* Close (is_exit=0) or Exit (is_exit!=0). Exit also requests shutdown.
 * Either one silences the last-window zenity follow-up. */
void hush_relay_note_leave(int is_exit);

/* SIGTERM then SIGKILL tracked children. Linux also sweeps leftover --app windows. */
void hush_relay_reap_children(void);

#endif /* HUSH_RELAY_H */
