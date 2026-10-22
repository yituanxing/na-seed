#!/bin/sh
# Existing native complete full-kernel build, real GNU/Linux ELF PID1 and
# same pinned BusyBox ELF/CPIO as all 13 architecture-parity programs.
# Stage one new EFI disk for the *interactive* userspace path.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m18-port-interactive.sh riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    riscv64) efi_name=BOOTRISCV64.EFI;fw=/usr/share/qemu-efi-riscv64 ;;
    arm64) efi_name=BOOTAA64.EFI;fw=/usr/share/qemu-efi-aarch64 ;;
    *) exit 2 ;;
esac
# Native ELF toolchain calls this architecture arm64, but existing
# Limine/QEMU port output uses the AArch64 ISA name.
case "$port" in
    arm64) dir=build/port-aarch64/full ;;
    riscv64) dir=build/port-riscv64/full ;;
esac
work="$dir/matrix-source"
disk="$work/m18-real-ash.img"
efi="build/deps/limine-binary/$efi_name"
test -s "$efi" && test -s "$dir/kernel.elf" &&
    test -s "$work/rootfs.cpio" &&
    test -s "build/source-busybox/$port/m18-ash-init"
truncate -s 128M "$disk"
sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
mformat -i "$disk@@1048576" -F ::
mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk@@1048576" "$efi" "::/EFI/BOOT/$efi_name"
mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
mcopy -i "$disk@@1048576" "$work/rootfs.cpio" ::/boot/rootfs.cpio
config="$work/m18-real-ash.conf"
cat > "$config" <<'CONF'
timeout: 0
/NaSeed native interactive BusyBox ash
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/m18-ash-init
    module_path: boot():/boot/rootfs.cpio
CONF
mcopy -i "$disk@@1048576" "$config" ::/boot/limine/limine.conf
if [ "$port" = riscv64 ]; then
    cp "$fw/RISCV_VIRT_VARS.fd" "${disk%.img}.vars.fd"
fi
python3 scripts/m18-ash-serial.py "$disk" "$port"
