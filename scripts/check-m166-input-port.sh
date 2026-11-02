#!/bin/sh
# Host exercise of the actual input core/evdev and Linux VirtIO-input port.
# Cross-ISA syntax is independently checked with clang in CI, and native
# QEMU input-send-event + physical framebuffer checks are a separate gate.
set -eu
mkdir -p build/m166-input-host
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-m166-input-core.c kernel/drivers/input/core.c \
    kernel/drivers/input/evdev.c kernel/mm/heap.c kernel/lib/string.c \
    -o build/m166-input-host/input-core
build/m166-input-host/input-core
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    -I third_party/unikraft/include -I ports/unikraft/include \
    scripts/test-m166-virtio-input-port.c \
    kernel/drivers/input/virtio_input.c ports/unikraft/split_queue.c \
    kernel/drivers/virtio/transport.c kernel/drivers/port/dma.c \
    kernel/lib/string.c -o build/m166-input-host/virtio-input
build/m166-input-host/virtio-input
if command -v clang >/dev/null 2>&1; then
    for target in x86_64-unknown-none-elf riscv64-unknown-elf aarch64-none-elf; do
        clang --target="$target" -std=c11 -Wall -Wextra -Werror \
            -ffreestanding -fno-builtin -DNAS_ENABLE_VIRTIO_INPUT=1 \
            -I kernel -I third_party/unikraft/include \
            -I ports/unikraft/include -I ports/uboot/compat \
            -I ports/lwip/include -I third_party/lwip/src/include \
            -fsyntax-only \
            kernel/drivers/core/chrdev.c kernel/drivers/input/core.c \
            kernel/drivers/input/evdev.c \
            kernel/drivers/input/virtio_input.c kernel/fs/input_chrdev.c \
            kernel/fs/syscall.c kernel/fs/file_io.c \
            kernel/fs/metadata_syscall.c kernel/init/main.c \
            kernel/task/scheduler_service.c ports/lwip/network_boot.c
    done
fi
echo '[M166 HOST PORT PASS] Linux input core, evdev, two VirtIO vrings and native ISA syntax'
