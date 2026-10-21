#!/bin/sh
# Architecture parity regression: run *unmodified* official Debian RV64
# BusyBox through the SAME shared kernel as x86 for a growing workload matrix.
# No guest-side test-specific kernel shortcuts. Strict stdout + exit checks.
set -eu
sh scripts/fetch-debian-riscv64.sh
test -s build/port-riscv64/full/kernel.elf || sh scripts/build-riscv64-full.sh
dir=build/port-riscv64/full
work="$dir/matrix"
mkdir -p "$work"
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTRISCV64.EFI
test -s "$efi"
fw=/usr/share/qemu-efi-riscv64
test -s "$fw/RISCV_VIRT_CODE.fd"
# The rootfs is built only from verified native BusyBox plus actual bytes
# in the archive. The same kernel bootfs.c handles archive/file lookup.
root="$work/rootfs"
mkdir -p "$root/bin" "$root/etc" "$root/tmp"
cp "$dir/busybox" "$root/bin/busybox"
printf 'naseed\n' > "$root/etc/hostname"
chmod 755 "$root" "$root/bin" "$root/etc" "$root/bin/busybox"
chmod 644 "$root/etc/hostname"
chmod 1777 "$root/tmp"
for name in cat wc head grep ls sh; do
    ln -sf busybox "$root/bin/$name"
done
touch -t 197001010000 "$root" "$root/bin" "$root/etc" "$root/tmp" \
     "$root/bin/busybox" "$root/etc/hostname"
for name in cat wc head grep ls sh; do
    touch -h -t 197001010000 "$root/bin/$name"
done
(
    cd "$root"
    {
        printf '%s\n' . bin bin/busybox etc etc/hostname tmp
        for name in cat wc head grep ls sh; do printf '%s\n' "bin/$name"; done
    } | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
# Two boot modes: real module first; and *archive-only* Limine handoff,
# analogous to the x86 initramfs tests. Command selection differs, kernel
# ELF loader / FS and task runtime are identical.
boot_case() {
    name=$1 mode=$2 args=$3 expected=$4
    disk="$work/$name.img"
    config="$work/$name.conf"
    log="$work/$name.serial.log"
    clean="$work/$name.clean.log"
    truncate -s 128M "$disk"
    sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
    mformat -i "$disk@@1048576" -F ::
    mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
    mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTRISCV64.EFI
    mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
    if [ "$mode" = cpio ]; then
        mcopy -i "$disk@@1048576" "$work/rootfs.cpio" ::/boot/rootfs.cpio
        module='rootfs.cpio'
    else
        mcopy -i "$disk@@1048576" "$dir/busybox" ::/boot/busybox
        module='busybox'
    fi
    {
        printf '%s\n' 'timeout: 0' '/NaSeed RV64 native BusyBox workload'
        printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
        printf '    cmdline: naseed.argv=/bin/busybox %s\n' "$args"
        printf '    module_path: boot():/boot/%s\n' "$module"
    } > "$config"
    mcopy -i "$disk@@1048576" "$config" ::/boot/limine/limine.conf
    cp "$fw/RISCV_VIRT_VARS.fd" "$work/$name.vars.fd"
    set +e
    timeout 90s qemu-system-riscv64 \
        -machine virt,acpi=off -m 512M -smp 1 \
        -display none -serial stdio -monitor none \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$fw/RISCV_VIRT_CODE.fd" \
        -drive if=pflash,format=raw,unit=1,file="$work/$name.vars.fd" \
        -drive if=none,id=esp,format=raw,file="$disk" \
        -device virtio-blk-device,drive=esp > "$log" 2>&1 &
    pid=$!
    sh scripts/port-boot-until.sh "$log" "$pid" 90 \
      '[INIT] creating common task/VM and architecture user entry' \
      '[PASS] cmdline userspace exit 0' \
      '[RV64] userspace debug-exit status=0'
    monitor=$?
    kill "$pid" 2>/dev/null || :
    wait "$pid" 2>/dev/null || :
    set -e
    tr -d '\r' < "$log" > "$clean"
    if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$clean" || \
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=/bin/busybox $args" "$clean"; then
        echo "[RV64 BUSYBOX FAIL] real $mode BusyBox $args did not cleanly exit" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    if [ "$expected" != '-' ] && ! grep -Fxq "$expected" "$clean"; then
        echo "[RV64 BUSYBOX FAIL] wrong stdout for $mode $args: expected [$expected]" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    if [ "$mode" = cpio ] && ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean"; then
        echo '[RV64 BUSYBOX FAIL] CPIO mount marker missing' >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    echo "[ACCEPTED] native RV64 BusyBox ($mode): $args"
}
# Native ABI+kernel mechanism checks, run as identical unmodified BusyBox
# applets to the x86 matrix, never hand-coded substitute ELF programs.
boot_case echo   direct 'echo hello'        hello
boot_case uname  direct 'uname -s'          Linux
boot_case pwd    direct 'pwd'              /
boot_case id     direct 'id -u'            0
boot_case true   direct 'true'             -
boot_case printf direct 'printf rv-printf\\\\n' rv-printf

# Actual CPIO: exercised via the same bootfs, openat, stat, getdents, read,
# fd and VFS code as the established x86 Alpine BusyBox tests.
boot_case root   cpio 'ls -1 /'           bin
boot_case bin    cpio 'ls -1 /bin'        busybox
boot_case cat    cpio 'cat /etc/hostname' naseed
boot_case head   cpio 'head -n 1 /etc/hostname' naseed
boot_case grep   cpio 'grep -n naseed /etc/hostname' 1:naseed
boot_case wc     cpio 'wc -c /etc/hostname' '7 /etc/hostname'
echo '[PASS] RV64 official BusyBox direct+CPIO full common-kernel matrix'
