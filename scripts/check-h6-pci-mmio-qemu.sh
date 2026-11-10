#!/bin/sh
# Positive H6 QEMU test: known relocated NVMe BAR ownership remains pinned
# until a real MMIO unmap primitive exists. Storage READ remains read-only.
set -eu

iso=${1:-build/h6-qemu-only/naseed.iso}
serial_log=${2:-build/h6-qemu-only/h6-qemu-uefi-serial.log}
if [ "$iso" != build/h6-qemu-only/naseed.iso ] ||
   [ "$serial_log" != build/h6-qemu-only/h6-qemu-uefi-serial.log ] ||
   [ -L "$serial_log" ] || [ ! -s "$iso" ]; then
    echo 'H6 positive: only fixed nonempty build/h6-qemu-only ISO/log permitted' >&2
    exit 2
fi
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
   [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] ||
   [ "${NAS_QEMU_H2_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H4_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H5_PCI_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H6_TWO_NVME:-0}" != 0 ]; then
    echo 'H6 positive: unexpected ambient disk/controller override' >&2
    exit 2
fi

sh scripts/check-h4-nvme-seed.sh
mkdir -p "$(dirname "$serial_log")"
set +e
NAS_QEMU_H5_PCI_NVME=1 timeout 55s sh scripts/run.sh "$iso" >"$serial_log" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "H6 positive: QEMU terminated unexpectedly rc=$rc" >&2
    tail -120 "$serial_log" >&2
    exit "$rc"
fi

for required in \
    '[H2 PCI RESOURCE VERIFIED] NVMe=1' \
    '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=' \
    '[H6 PCI MMIO PINNED] bus=0 slot=5 function=0 BAR0=' \
    '[H4 NVME LBA VERIFIED] NSID=1 LBA0+LBA1 exact fixture bytes' \
    '[H4 BLOCK PORT VERIFIED] nas_block_read x2, writes=-EROFS' \
    '[H4 COMPLETE] QEMU-only disposable namespace read; no LBA writes'; do
    if ! grep -Fq "$required" "$serial_log"; then
        echo "H6 positive: missing real QEMU guest proof: $required" >&2
        tail -120 "$serial_log" >&2
        exit 1
    fi
done

if grep -Fq '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=0x000000c000000000' "$serial_log" ||
   grep -Eq '^\[FAIL\]|^\[H[3456] .*FAIL\]' "$serial_log"; then
    echo 'H6 positive: old BAR or guest failure' >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

sh scripts/check-h4-nvme-seed.sh
echo 'H6 POSITIVE PASS: relocated QEMU NVMe read-only LBA with explicit pinned BAR ownership'
tail -120 "$serial_log"
