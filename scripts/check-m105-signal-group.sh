#!/bin/sh
# Execute production signal group routing against deterministic real Task
# lifecycles. This is HOST evidence; the separate source BusyBox probe
# supplies actual three-ISA native QEMU proof.
set -eu
mkdir -p build/m105-signal-group
${CC:-cc} -O2 -std=c11 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -I kernel \
    scripts/test-m105-signal-group-host.c kernel/task/signal.c \
    -Wl,--gc-sections -o build/m105-signal-group/host
timeout 30s build/m105-signal-group/host
echo '[M105 HOST PASS] production signal.c TGID routing and teardown model'
