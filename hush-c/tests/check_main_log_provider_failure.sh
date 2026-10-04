#!/bin/sh
# Locks the main-log bar: missing-runtime text is on the root card.
set -eu
cd "$(dirname "$0")/.."
if ! command -v node >/dev/null 2>&1; then
  echo "main-log failure check: node is required" >&2
  exit 1
fi
node tests/check_main_log_provider_failure.mjs
