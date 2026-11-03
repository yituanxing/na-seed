#!/bin/sh
# Run production IRQ registry and PCI transport tests without launching QEMU.
set -eu
mkdir -p build/m175
cc -std=c11 -O2 -Wall -Wextra -Werror -I kernel \
    scripts/test-m175-irq-core.c kernel/drivers/core/irq.c \
    -o build/m175/test-irq-core
build/m175/test-irq-core
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-virtio-pci.c kernel/drivers/virtio/pci.c \
    -o build/m175/test-virtio-pci
build/m175/test-virtio-pci
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -DNAS_ENABLE_VIRTIO_INPUT=1 -I kernel \
    -I third_party/unikraft/include -I ports/unikraft/include \
    scripts/test-m175-irq-input.c \
    kernel/drivers/input/virtio_input.c ports/unikraft/split_queue.c \
    kernel/drivers/virtio/transport.c kernel/drivers/port/dma.c \
    kernel/lib/string.c -o build/m175/test-irq-input
build/m175/test-irq-input
python3 scripts/check-three-isa-core.py
python3 scripts/check-architecture-boundaries.py
