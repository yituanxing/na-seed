#!/bin/sh
# Fail-closed Host contract before expensive genuine three-ISA QEMU guests.
# Real QEMU + persisted host-visible disk bytes are separately mandatory in
# probe-m178-blk-irq.sh; simulation alone never proves hardware completion.
set -eu
mkdir -p build/m178

test -s scripts/test-m178-block-request.c || {
    echo '[M178 HOST FAIL] missing actual BlockRequest ownership fixture' >&2
    exit 1
}
test -s scripts/test-m178-virtio-blk.c || {
    echo '[M178 HOST FAIL] missing original asynchronous block driver fixture' >&2
    exit 1
}

cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I . \
    scripts/test-m178-block-request.c -o build/m178/test-block-request
ASAN_OPTIONS=detect_leaks=0 build/m178/test-block-request

cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -I . -I kernel -I third_party/unikraft/include -I ports/unikraft/include \
    scripts/test-m178-virtio-blk.c -o build/m178/test-virtio-blk
build/m178/test-virtio-blk good
for scenario in bad_used bad_status timeout no_irq alloc_fail read_only \
                no_flush remove_busy bad_head; do
    build/m178/test-virtio-blk "$scenario"
done

# The same production async class must retain IRQ, ring and DMA ownership
# if a provider refuses release during remove or failed attach. Retry on
# the original singleton before reusing its storage for a new bus probe.
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
    -I . -I kernel -I third_party/unikraft/include -I ports/unikraft/include \
    scripts/test-m184-async-blk-lifetime.c -o build/m178/test-m184-async-blk-lifetime
build/m178/test-m184-async-blk-lifetime

sh -n scripts/probe-m178-blk-irq.sh
sh -n scripts/probe-m177-gpu-2d.sh
sh -n scripts/probe-m174-batch.sh
sh -n scripts/run.sh
sh -n scripts/probe-riscv64-busybox-matrix.sh
sh -n scripts/probe-aarch64-busybox-matrix.sh

# Preserve all already-proven original U-Boot, input, net and GPU boundaries.
# This gate cannot override older host failures or silently skip them.
sh scripts/check-m177-gpu.sh
echo '[M178 HOST BLOCK PASS] owned BlockRequest + independent concurrent VirtIO-BLK DMA, IRQ/deferred, exact-once fail-closed and original M177 GPU/M176 net/M175 input'
