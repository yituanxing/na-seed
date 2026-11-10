#!/bin/sh
# H5 independently proves a relocated QEMU NVMe PCI function goes through
# discovered BAR0 to the unchanged NVMe READ -> nas_block_read path.
set -eu

iso=${1:-build/h5-qemu-only/naseed.iso}
serial_log=${2:-build/h5-qemu-only/h5-qemu-uefi-serial.log}
if [ "$iso" != build/h5-qemu-only/naseed.iso ] ||
   [ "$serial_log" != build/h5-qemu-only/h5-qemu-uefi-serial.log ] ||
   [ -L "$serial_log" ]; then
    echo 'H5: acceptance accepts only fixed build/h5-qemu-only paths' >&2
    exit 2
fi
if [ ! -s "$iso" ]; then
    echo "H5: missing/empty UEFI ISO: $iso" >&2
    exit 2
fi

# No ambient real/unknown image may replace our fixed 1MiB test namespace.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
   [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] ||
   [ "${NAS_QEMU_H2_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H4_NVME:-0}" != 0 ] ||
   [ "${NAS_QEMU_H5_PCI_NVME:-0}" != 0 ]; then
    echo 'H5: unexpected ambient storage/controller override' >&2
    exit 2
fi

sh scripts/check-h4-nvme-seed.sh
mkdir -p "$(dirname "$serial_log")"

set +e
NAS_QEMU_H5_PCI_NVME=1 timeout 55s sh scripts/run.sh "$iso" >"$serial_log" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "H5: QEMU failed with rc=$rc" >&2
    tail -120 "$serial_log" >&2
    exit "$rc"
fi

for required in \
    '[H1 ECAM VERIFIED]' \
    '[H2 PCI RESOURCE VERIFIED] NVMe=1' \
    '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=' \
    '[H4 NVME LBA VERIFIED]' \
    '[H4 BLOCK PORT VERIFIED]' \
    '[H4 COMPLETE] QEMU-only disposable namespace read; no LBA writes'; do
    if ! grep -Fq "$required" "$serial_log"; then
        echo "H5: missing actual relocated-QEMU guest proof: $required" >&2
        tail -120 "$serial_log" >&2
        exit 1
    fi
done

# H4's qualified controller was mapped at 0xc000000000. Besides relocating
# the BDF, this fixture must actually exercise a newly assigned MMIO BAR;
# otherwise a regression could still accidentally use the old hardcoded PA.
if grep -Fq '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=0x000000c000000000' "$serial_log"; then
    echo 'H5: BAR0 did not move from H4 fixed physical address' >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

if grep -Eq '^\[FAIL\]|^\[H[345] .*FAIL\]|^\[H[45] ERROR\]' "$serial_log"; then
    echo 'H5: guest reported error despite apparent success markers' >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

sh scripts/check-h4-nvme-seed.sh
echo 'H5 PASS: relocated 00:05.0 NVMe + decoy PCI slot + read-only BlockDevice LBA0/LBA1'
tail -120 "$serial_log"
