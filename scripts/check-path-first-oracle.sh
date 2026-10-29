#!/bin/sh
# Validate Linux's actual namespace semantics before changing NaSeed's
# Path/dirfd implementation. This is a host oracle, not a QEMU PASS.
set -eu
test "${0#*/}" != "$0" || { echo 'run from repository root' >&2; exit 2; }
scratch=$(mktemp -d /tmp/naseed-path-first-oracle.XXXXXX)
trap 'rm -f "$scratch/oracle"; rmdir "$scratch"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror \
  tests/path-first-linux-oracle.c -o "$scratch/oracle"
"$scratch/oracle"
# Real NaSeed VFS object + namei code in a deterministic provider fixture.
# It does not pretend that the host Linux oracle already ran inside NaSeed.
sh scripts/check-vfs-provider.sh
echo '[PATH FIRST HOST PASS] real Linux oracle and genuine NaSeed host VFS provider'
