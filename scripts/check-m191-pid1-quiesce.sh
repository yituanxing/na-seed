#!/bin/sh
# Host execution of the production shared SIGKILL/exit_group quiescence gate.
set -eu
mkdir -p build/m191-pid1-quiesce
# Keep compiler scratch under the test's own build directory; CI runners
# and local cross-build workers can have an exhausted shared /tmp tmpfs.
TMPDIR="$PWD/build/m191-pid1-quiesce"
export TMPDIR
${CC:-cc} -O2 -std=c11 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -I kernel \
    scripts/test-m191-pid1-quiesce.c kernel/task/signal.c \
    -Wl,--gc-sections -o build/m191-pid1-quiesce/host
timeout 30s build/m191-pid1-quiesce/host
${CC:-cc} -O2 -std=c11 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -I kernel \
    -DNAS_ENABLE_M191_ALPINE_ROOT=1 -DNAS_ENABLE_LWEXT4=1 \
    -DNAS_ENABLE_M179_FS_IRQ=1 -DNAS_ENABLE_M180_FS_USER_IRQ=1 \
    scripts/test-m191-pid1-deferred-order.c kernel/init/main.c \
    -Wl,--gc-sections -o build/m191-pid1-quiesce/deferred-order
timeout 30s build/m191-pid1-quiesce/deferred-order --armed
timeout 30s build/m191-pid1-quiesce/deferred-order --unarmed
timeout 30s build/m191-pid1-quiesce/deferred-order --poweroff
timeout 30s build/m191-pid1-quiesce/deferred-order --poweroff-armed
timeout 30s build/m191-pid1-quiesce/deferred-order --poweroff-live
timeout 30s build/m191-pid1-quiesce/deferred-order --poweroff-live-armed
