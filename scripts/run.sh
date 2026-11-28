#!/bin/sh
# Minimal x86_64 QEMU/OVMF boot runner. No fixture or hidden profile.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 <iso>" >&2; exit 2; }
iso=$1
[ -f "$iso" ] || { echo "ISO missing: $iso" >&2; exit 2; }
ovmf=${NAS_OVMF_CODE:-}
if [ -z "$ovmf" ]; then
    for path in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd; do
        if [ -f "$path" ]; then ovmf=$path; break; fi
    done
fi
[ -n "$ovmf" ] && [ -f "$ovmf" ] || { echo "OVMF firmware not found" >&2; exit 2; }
# Opt-in CI experiment: capture real guest exceptions/PC to distinguish
# CPU faults from host ABI / heap-code deadlock. No default runtime change.
if [ "${NAS_EXPERIMENT_TGOS_QEMU_TRACE:-0}" = 1 ]; then
    set -- -d int,guest_errors -D build/tgos-qemu-exceptions.log
else
    set --
fi
exec qemu-system-x86_64 "$@" -M q35 -m "${NAS_QEMU_RAM:-256M}" -cpu qemu64 \
    -display none -monitor none -serial stdio -no-reboot -no-shutdown \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" \
    -cdrom "$iso"
