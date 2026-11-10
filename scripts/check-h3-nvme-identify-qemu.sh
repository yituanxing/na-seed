#!/bin/sh
# H3 is an opt-in QEMU-only NVMe *controller Identify* acceptance.
# No namespace or storage image is presented to the guest.  Boot evidence is
# mandatory: a QEMU timeout alone is not an acceptance result.
set -eu

iso=${1:-build/h3-qemu-only/naseed.iso}
serial_log=${2:-build/h3-qemu-only/h3-qemu-uefi-serial.log}

if [ ! -s "$iso" ]; then
    echo "H3: missing/empty UEFI ISO: $iso" >&2
    exit 2
fi

# Existing block-image overrides would silently invalidate the no-storage
# fixture.  Don't let ambient test variables redefine H3's safety boundary.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
   [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ]; then
    echo 'H3: external block image forbidden in controller-only acceptance' >&2
    exit 2
fi

mkdir -p "$(dirname "$serial_log")"

set +e
NAS_QEMU_H2_NVME=1 timeout 50s sh scripts/run.sh "$iso" >"$serial_log" 2>&1
rc=$?
set -e

if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "H3: QEMU failed with rc=$rc" >&2
    tail -110 "$serial_log" >&2
    exit "$rc"
fi

for required in \
    '[H1 ECAM VERIFIED]' \
    '[H2 PCI RESOURCE VERIFIED] NVMe=1' \
    '[H3 NVME IDENTIFY VERIFIED]' \
    '[H3 NVME] no namespace block attach; no LBA I/O' \
    '[H3 COMPLETE] QEMU-only NVMe Identify; no namespace LBA I/O'; do
    if ! grep -Fq "$required" "$serial_log"; then
        echo "H3: missing actual boot evidence: $required" >&2
        tail -110 "$serial_log" >&2
        exit 1
    fi
done

if grep -Eq '^\[FAIL\]|^\[H3 NVME (FAIL|ERROR)\]' "$serial_log"; then
    echo 'H3: guest reported failure despite other markers' >&2
    tail -110 "$serial_log" >&2
    exit 1
fi

echo 'H3 PASS: QEMU UEFI NVMe Identify completed; no namespace/block attachment/LBA I/O'
tail -95 "$serial_log"
