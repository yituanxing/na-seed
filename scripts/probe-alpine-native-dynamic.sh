#!/bin/sh
# Diagnostic stage: pinned-version Alpine ET_DYN BusyBox on *full* NaSeed
# kernel, actual EL0/U-mode and native musl. No hand-written ELF substitute.
# Exit nonzero if the guest fails; CI may mark this exploratory step optional.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-alpine-native-dynamic.sh arm64|riscv64' >&2; exit 2; }
port=$1
case "$port" in
    arm64)
        arch=aarch64
        efi=BOOTAA64.EFI
        loader=ld-musl-aarch64.so.1
        qemu=qemu-system-aarch64
        debug_tag='[ARM64] userspace debug-exit status=0'
        firmware=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
        test -s "$firmware" ;;
    riscv64)
        arch=riscv64
        efi=BOOTRISCV64.EFI
        loader=ld-musl-riscv64.so.1
        qemu=qemu-system-riscv64
        debug_tag='[RV64] userspace debug-exit status=0'
        firmware=/usr/share/qemu-efi-riscv64
        test -s "$firmware/RISCV_VIRT_CODE.fd" ;;
    *) echo "unsupported target: $port" >&2; exit 2 ;;
esac
dir="build/port-$arch/full"
test -s "$dir/kernel.elf" || sh "scripts/build-$arch-full.sh"
work="$dir/alpine-native"
mkdir -p "$work"
sh scripts/fetch-alpine-native.sh "$port" "$work/native"
sh scripts/stage-alpine-native-rootfs.sh \
    "$work/native/bin/busybox" "$work/native/lib/$loader" "$work"
limine=build/deps/limine-binary
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
test -s "$limine/$efi"
case_name=${ALPINE_NATIVE_CASE:-echo}
case "$case_name" in *[!a-zA-Z0-9_-]*|'')
    echo '[ALPINE NATIVE] invalid testcase name' >&2
    exit 2 ;;
esac
disk="$work/$case_name.img"
config="$work/$case_name.conf"
log="$work/$case_name.serial.log"
clean="$work/$case_name.clean.log"
truncate -s 128M "$disk"
sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" > /dev/null
mformat -i "$disk@@1048576" -F ::
mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk@@1048576" "$limine/$efi" "::/EFI/BOOT/$efi"
mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
mcopy -i "$disk@@1048576" "$work/rootfs.cpio" ::/boot/rootfs.cpio
args=${ALPINE_NATIVE_ARGS:-'echo hello'}
{
    printf '%s\n' 'timeout: 0' '/NaSeed native Alpine dynamic ELF probe'
    printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
    printf '    cmdline: naseed.argv=/bin/busybox %s\n' "$args"
    printf '%s\n' '    module_path: boot():/boot/rootfs.cpio'
} > "$config"
mcopy -i "$disk@@1048576" "$config" ::/boot/limine/limine.conf
if [ "$port" = arm64 ]; then
    timeout 40s "$qemu" \
        -machine virt -cpu cortex-a57 -m 512M -smp 1 \
        -display none -serial stdio -monitor none \
        -semihosting-config enable=on,target=native -bios "$firmware" \
        -drive if=none,id=esp,format=raw,file="$disk" \
        -device virtio-blk-device,drive=esp > "$log" 2>&1 < /dev/null &
else
    cp "$firmware/RISCV_VIRT_VARS.fd" "$work/vars.fd"
    timeout 55s "$qemu" \
        -machine virt,acpi=off -m 512M -smp 1 \
        -display none -serial stdio -monitor none \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware/RISCV_VIRT_CODE.fd" \
        -drive if=pflash,format=raw,unit=1,file="$work/vars.fd" \
        -drive if=none,id=esp,format=raw,file="$disk" \
        -device virtio-blk-device,drive=esp > "$log" 2>&1 < /dev/null &
fi
pid=$!
set +e
sh scripts/port-boot-until.sh "$log" "$pid" 35 \
    '[DYN] real musl interpreter loaded, entry=' \
    '[PASS] cmdline userspace exit 0' "$debug_tag"
monitor=$?
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
set -e
tr -d '\r' < "$log" > "$clean"
expected=${ALPINE_NATIVE_EXPECT:-hello}
if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$clean" ||
   ! grep -Fxq "$expected" "$clean"; then
    echo "[ALPINE NATIVE BLOCKED] $arch dynamic BusyBox $args" >&2
    tail -125 "$clean" >&2
    exit 1
fi
echo "[ALPINE NATIVE PASS] $arch genuine Alpine musl dynamic BusyBox [$case_name]: $args"
