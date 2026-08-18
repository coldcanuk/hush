/* hush_relay.h: poll-based relay server and connection dispatch for Hush. */

#ifndef HUSH_RELAY_H
#define HUSH_RELAY_H

#include <stdint.h>
#include "hush_status.h"

/* Run the relay on given TCP port. Blocks until error or signal.
 * If open_ui is non-zero, open the local chat UI in a browser.
 * If the port is already taken and open_ui is set, reopen the UI and return OK. */
hush_status_t hush_relay_run(uint16_t port, int open_ui);

#endif /* HUSH_RELAY_H */
