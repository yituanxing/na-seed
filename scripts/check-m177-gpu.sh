#!/bin/sh
# Host-only fail-closed gate for M177 before three expensive real QEMU runs.
# The GPU fixture links the production class driver verbatim and emulates
# protocol responses; only the native jobs can prove real QEMU screen pixels.
set -eu
mkdir -p build/m177
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -I kernel -I third_party/unikraft/include -I ports/unikraft/include \
    scripts/test-m177-gpu-protocol.c -o build/m177/test-gpu-protocol
build/m177/test-gpu-protocol good
for scenario in bad_response bad_fence short_reply missing_scanout \
                bad_create bad_attach bad_scanout timeout alloc_fail \
                no_irq_source no_primary runtime_fence runtime_timeout \
                runtime_dirty_epoch pixel_release_fail entry_release_fail \
                command_release_fail response_release_fail ring_release_fail \
                ring_init_orphan alloc_rollback_fail irq_unbind_fail; do
    build/m177/test-gpu-protocol "$scenario"
done
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I kernel \
    scripts/test-m177-display-handoff.c \
    kernel/drivers/display/fbdev.c \
    kernel/drivers/display/fbdev_chrdev.c \
    kernel/drivers/core/chrdev.c -o build/m177/test-display-handoff
ASAN_OPTIONS=detect_leaks=0 build/m177/test-display-handoff handoff
ASAN_OPTIONS=detect_leaks=0 build/m177/test-display-handoff locked
sh -n scripts/probe-m177-gpu-2d.sh
sh -n scripts/probe-m174-batch.sh
sh -n scripts/run.sh
sh -n scripts/probe-riscv64-busybox-matrix.sh
sh -n scripts/probe-aarch64-busybox-matrix.sh
sh scripts/check-m172-device-display.sh
sh scripts/check-m176-net-irq.sh
echo '[M177/M181 HOST GPU PASS] real driver protocol, IRQ-only runtime completion, dirty epochs, timeout/fence DMA lifetime, one-shot DisplayDevice takeover, unchanged M176 IRQ/network and M172 fbdev/evdev'
