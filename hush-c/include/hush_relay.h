/* hush_relay.h: poll-based relay server and connection dispatch for Hush. */

#ifndef HUSH_RELAY_H
#define HUSH_RELAY_H

#include <stdint.h>
#include "hush_status.h"

/* Run the relay on given TCP port. Blocks until error or signal. */
hush_status_t hush_relay_run(uint16_t port);

#endif /* HUSH_RELAY_H */
