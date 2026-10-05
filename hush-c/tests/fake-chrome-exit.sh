#!/bin/sh
# Exits immediately with code 3; counts launches for the #265 pin.
set -eu
log="${HUSH_FAKE_CHROME_LAUNCH_LOG:-}"
if [ -n "$log" ]; then
    printf 'launch\n' >> "$log"
fi
exit 3
