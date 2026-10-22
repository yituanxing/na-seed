#!/bin/sh
# The EXACT same script runs with the unmodified upstream BusyBox ash
# on host Linux and inside the NaSeed guest. No kernel-facing test hooks.
# The child forks FIRST on NaSeed, writes a real tmpfs ready sentinel,
# and enters a POSIX-shell builtin loop that issues no syscalls.
ready=/tmp/m28-preempt-$$
(printf ready > "$ready"; while :; do :; done) &
hog=$!
# A cooperative child-first scheduler cannot run this parent once the
# child reaches its CPU loop. A real hardware timer must preempt it.
while [ ! -f "$ready" ]; do :; done
kill -9 "$hog" || exit 1
rm -f "$ready" || exit 1
echo cpu-hog-did-not-starve-ash
