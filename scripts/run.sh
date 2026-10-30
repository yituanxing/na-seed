#!/bin/sh
set -eu
iso="$1"
ovmf=""
for candidate in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd; do
    if [ -f "$candidate" ]; then ovmf="$candidate"; break; fi
done
if [ -z "$ovmf" ]; then
    echo "OVMF not found. Install the ovmf package." >&2
    exit 2
fi
# An explicit raw image is a real block device, NOT a Limine RAM module.
# Default tests retain the original QEMU options and hardware set.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    test -s "$NAS_VIRTIO_BLK_IMAGE"
    set -- -drive "if=none,id=naseed_ext4,format=raw,file=$NAS_VIRTIO_BLK_IMAGE" \
           -device virtio-blk-pci,drive=naseed_ext4,disable-legacy=on
else
    set --
fi
exec qemu-system-x86_64 -M q35 -m "${NAS_ACCEPTANCE_QEMU_RAM:-256M}" -cpu qemu64 \
    -display none -monitor none -serial stdio -no-reboot -no-shutdown \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" -cdrom "$iso" "$@"
