#!/bin/sh
# Deterministic host test for Linux timerfd shared File semantics.
set -eu
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -ffreestanding -I kernel \
    scripts/test-timerfd-host.c kernel/fs/timerfd.c -o "$tmp/timerfd"
"$tmp/timerfd"
echo '[TIMERFD HOST PASS] clock, expiry, poll, read, dup/close/reuse, three-ISA shared provider'
