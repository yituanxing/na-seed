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
exec qemu-system-x86_64 -M q35 -m 256M -cpu qemu64 \
    -display none -monitor none -serial stdio -no-reboot -no-shutdown \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" -cdrom "$iso"
