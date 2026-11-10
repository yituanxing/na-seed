#!/bin/sh
# Negative H6 QEMU test: two known controllers, no namespace/disk. A whole
# read-only PCI scan must refuse ambiguous ownership before controller writes.
set -eu

iso=${1:-build/h6-qemu-ambiguous/naseed.iso}
serial_log=${2:-build/h6-qemu-ambiguous/h6-ambiguous-uefi-serial.log}
if [ "$iso" != build/h6-qemu-ambiguous/naseed.iso ] ||
   [ "$serial_log" != build/h6-qemu-ambiguous/h6-ambiguous-uefi-serial.log ] ||
   [ -L "$serial_log" ] || [ ! -s "$iso" ]; then
    echo 'H6 negative: only fixed nonempty build/h6-qemu-ambiguous ISO/log permitted' >&2
    exit 2
fi

# No namespace, storage image, host disk path or other controller profile.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
   [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] ||
   [ "${NAS_QEMU_H2_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H4_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H5_PCI_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H6_TWO_NVME:-0}" != 0 ]; then
    echo 'H6 negative: ambient disk/controller override forbidden' >&2
    exit 2
fi

mkdir -p "$(dirname "$serial_log")"
set +e
NAS_QEMU_H6_TWO_NVME=1 timeout 55s sh scripts/run.sh "$iso" >"$serial_log" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "H6 negative: QEMU terminated unexpectedly rc=$rc" >&2
    tail -120 "$serial_log" >&2
    exit "$rc"
fi

for required in \
    '[H2 PCI RESOURCE] bus=0 slot=5 function=0' \
    '[H2 PCI RESOURCE] bus=0 slot=6 function=0' \
    '[H6 PCI FAIL-CLOSED] duplicate NVMe candidates; controller writes=0 DMA=0' \
    '[H6 AMBIGUOUS NVME COMPLETE] two QEMU NVMe controllers refused before controller writes/DMA'; do
    if ! grep -Fq "$required" "$serial_log"; then
        echo "H6 negative: missing real QEMU refusal proof: $required" >&2
        tail -120 "$serial_log" >&2
        exit 1
    fi
done

candidates=$(grep -Fc '[H2 NVME CANDIDATE]' "$serial_log" || true)
if [ "$candidates" -ne 2 ]; then
    echo "H6 negative: expected exactly two discovered NVMe controllers, got $candidates" >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

if grep -Eq '^\[FAIL\]|^\[H3 NVME FAIL\]|^\[H6 AMBIGUOUS NVME FAIL\]|^\[H5 PCI BAR QUALIFIED\]|^\[H6 PCI MMIO PINNED\]|^\[H3 NVME IDENTIFY VERIFIED\]|^\[H4 NVME LBA VERIFIED\]|^\[H4 COMPLETE\]' "$serial_log"; then
    echo 'H6 negative: controller touched or unexpected failure/success marker appeared' >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

echo 'H6 NEGATIVE PASS: two QEMU NVMe controllers discovered and refused before controller writes/DMA'
tail -120 "$serial_log"
