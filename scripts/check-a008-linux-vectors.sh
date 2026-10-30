#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -std=c11 -O2 -Wall -Wextra -Werror \
    scripts/test-a008-linux-vectors.c -o /tmp/naseed-a008-linux-vectors
/tmp/naseed-a008-linux-vectors
