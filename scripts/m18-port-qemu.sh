#!/bin/sh
# Forward the REAL QEMU virt serial stdin/stdout streams to M18's
# interactive host driver, with the existing reviewed firmware/CPU ABI.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: m18-port-qemu.sh riscv64|arm64 DISK' >&2; exit 2; }
port=$1 disk=$2
case "$port" in
    riscv64)
        fw=/usr/share/qemu-efi-riscv64
        exec qemu-system-riscv64 \
            -machine virt,acpi=off -m 512M -smp 1 -display none \
            -serial stdio -monitor none \
            -drive if=pflash,format=raw,unit=0,readonly=on,file="$fw/RISCV_VIRT_CODE.fd" \
            -drive if=pflash,format=raw,unit=1,file="${disk%.img}.vars.fd" \
            -drive if=none,id=esp,format=raw,file="$disk" \
            -device virtio-blk-device,drive=esp
        ;;
    arm64)
        fw=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
        exec qemu-system-aarch64 \
            -machine virt -cpu cortex-a57 -m 512M -smp 1 \
            -display none -serial stdio -monitor none \
            -semihosting-config enable=on,target=native -bios "$fw" \
            -drive if=none,id=esp,format=raw,file="$disk" \
            -device virtio-blk-device,drive=esp
        ;;
    *) echo "unsupported port: $port" >&2;exit 2;;
esac
