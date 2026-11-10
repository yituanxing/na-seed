#!/bin/sh
# H4 must prove real NVMe namespace LBA reads, not only QEMU boot/PCI scan.
set -eu

iso=${1:-build/h4-qemu-only/naseed.iso}
serial_log=${2:-build/h4-qemu-only/h4-qemu-uefi-serial.log}
if [ "$iso" != build/h4-qemu-only/naseed.iso ] ||
   [ "$serial_log" != build/h4-qemu-only/h4-qemu-uefi-serial.log ] ||
   [ -L "$serial_log" ]; then
    echo 'H4: acceptance accepts only fixed build/h4-qemu-only paths' >&2
    exit 2
fi
if [ ! -s "$iso" ]; then
    echo "H4: missing/empty UEFI ISO: $iso" >&2
    exit 2
fi

# Prevent ambient block-device images from silently changing the fixture.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
   [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] ||
   [ "${NAS_QEMU_H2_NVME:-0}" != 0 ]; then
    echo 'H4: unexpected ambient storage/controller override' >&2
    exit 2
fi

sh scripts/check-h4-nvme-seed.sh
mkdir -p "$(dirname "$serial_log")"

set +e
NAS_QEMU_H4_NVME=1 timeout 50s sh scripts/run.sh "$iso" >"$serial_log" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "H4: QEMU failed with rc=$rc" >&2
    tail -120 "$serial_log" >&2
    exit "$rc"
fi

for required in \
    '[H1 ECAM VERIFIED]' \
    '[H2 PCI RESOURCE VERIFIED] NVMe=1' \
    '[H4 NVME LBA VERIFIED]' \
    '[H4 BLOCK PORT VERIFIED]' \
    '[H4 COMPLETE] QEMU-only disposable namespace read; no LBA writes'; do
    if ! grep -Fq "$required" "$serial_log"; then
        echo "H4: missing real QEMU guest evidence: $required" >&2
        tail -120 "$serial_log" >&2
        exit 1
    fi
done

if grep -Eq '^\[FAIL\]|^\[H[34] .*FAIL\]|^\[H4 ERROR\]' "$serial_log"; then
    echo 'H4: guest reported a real error despite other markers' >&2
    tail -120 "$serial_log" >&2
    exit 1
fi

# The QEMU fixture is mounted read-only; no guest or host disk mutation.
sh scripts/check-h4-nvme-seed.sh
echo 'H4 PASS: actual QEMU NVMe LBA0/LBA1 data verified through read-only port'
tail -120 "$serial_log"
