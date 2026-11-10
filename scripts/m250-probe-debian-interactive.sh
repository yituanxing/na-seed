#!/bin/sh
# M250: physical QEMU UART -> test-only static CTTY PID1 -> execve() same PID1
# authentic Debian 13 glibc dash -i from the M249 real physical ext4 root.
# The UART driver sends original commands only on actual interactive prompts.
set -eu

[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/m250-probe-debian-interactive.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64) triplet=x86_64-linux-gnu; loader=ld-linux-x86-64.so.2; label=X86; cc=gcc ;;
    riscv64) triplet=riscv64-linux-gnu; loader=ld-linux-riscv64-lp64d.so.1; label=RV64; cc=riscv64-linux-gnu-gcc ;;
    arm64) triplet=aarch64-linux-gnu; loader=ld-linux-aarch64.so.1; label=ARM64; cc=aarch64-linux-gnu-gcc ;;
    *) echo "[M250 FAIL] unsupported architecture $port" >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

for tool in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum \
    mke2fs e2fsck debugfs sudo xorriso sgdisk mformat mmd mcopy "$cc"; do
    command -v "$tool" >/dev/null || { echo "[M250 FAIL] missing $tool" >&2; exit 1; }
done

work="build/m250-debian-interactive/$port"
root="$work/rootfs"
disk="$PWD/$work/m250-debian-root.img"
manifest="$work/source/manifest.json"
mkdir -p "$work"
python3 scripts/m248-fetch-debian-multiarch.py --arch "$port" --out "$work/source"
source=$(cd "$work/source/sysroot" && pwd -P)
test -s "$manifest"

# Same immutable original Debian ELF/glibc bytes as M249, signed and
# independently compared against M248 source manifest before and after QEMU.
mkdir -p "$root/usr/bin" "$root/usr/lib/$triplet" "$root/lib" "$root/lib64" \
    "$root/sbin" "$root/etc" "$root/dev" "$root/proc" "$root/sys" \
    "$root/var/tmp" "$root/tmp"
chmod 1777 "$root/tmp" "$root/var/tmp"
for name in dash true cat wc printf; do
    original="$source/usr/bin/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M250 FAIL] source path escape' >&2; exit 1 ;; esac
    cp -L "$original" "$root/usr/bin/$name"
    chmod 755 "$root/usr/bin/$name"
    cmp "$original" "$root/usr/bin/$name"
done
for name in libc.so.6 "$loader"; do
    original="$source/usr/lib/$triplet/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M250 FAIL] glibc source path escape' >&2; exit 1 ;; esac
    cp -L "$original" "$root/usr/lib/$triplet/$name"
    chmod 755 "$root/usr/lib/$triplet/$name"
    cmp "$original" "$root/usr/lib/$triplet/$name"
done
ln -sfn "../usr/lib/$triplet" "$root/lib/$triplet"
if [ "$port" = amd64 ]; then
    ln -sfn "../usr/lib/$triplet/$loader" "$root/lib64/$loader"
else
    ln -sfn "../usr/lib/$triplet/$loader" "$root/lib/$loader"
fi

# Keep M249's audited disk source metadata in its inherited validator;
# M250 deliberately does NOT boot this script or pretend direct-dash PID1.
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"
printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
printf '%s\n' M250-EXT4-INPUT > "$root/etc/m250-input"

# The ONLY added ELF is our explicitly NON-DEBIAN CTTY transport launcher.
# It sits on ext4, not in CPIO, and is fully replaced via execve by the
# unmodified original /usr/bin/dash -i with its genuine PT_INTERP loader.
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m250-debian-tty-launch.c -o "$root/sbin/m250-tty-launch"
chmod 755 "$root/sbin/m250-tty-launch"
if readelf -lW "$root/sbin/m250-tty-launch" | grep -q INTERP; then
    echo '[M250 FAIL] self-contained test-only CTTY launcher must be static' >&2
    exit 1
fi
if ! readelf -lW "$root/usr/bin/dash" | grep -q INTERP; then
    echo '[M250 FAIL] original glibc Debian dash PT_INTERP is absent' >&2
    exit 1
fi

sudo python3 scripts/m250-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest"
# mkfs under sudo creates root-owned image; real QEMU must be able to write.
sudo chown "$(id -u):$(id -g)" "$disk"
test -r "$disk" && test -w "$disk" || {
    echo '[M250 FAIL] physical ext4 image inaccessible to unprivileged QEMU' >&2
    exit 1
}

# This separate rescue archive never contains either Debian ELF or our
# launcher. There is no alternate CPIO shell success path in this test.
rescue="$work/rescue"
mkdir -p "$rescue"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/rescue-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/rescue-busybox" "$rescue"
test -s "$rescue/rootfs.cpio"
if cpio -it --quiet < "$rescue/rootfs.cpio" | grep -Eq \
    '(^|/)(sbin/m250-tty-launch|usr/bin/(dash|true|cat|wc|printf))$'; then
    echo '[M250 FAIL] physical ext4 guest binaries contaminated rescue CPIO' >&2
    exit 1
fi

case_id=m250-batch
case_file="$work/m250-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio --console - - /sbin/m250-tty-launch > "$case_file"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_CPIO="$PWD/$rescue/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$PWD/$work/rescue-busybox"
export BUSYBOX_PARITY_CASE="$case_id" BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST="$case_file"
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M250_QEMU_TIMEOUT:-300}
export NAS_SYSCALL_TRACE=1

if [ "$port" = amd64 ]; then
    . scripts/native-source-qemu.sh
    nas_source_build_kernel m250-debian
else
    case "$port" in
        riscv64) sh scripts/build-riscv64-full.sh ;;
        arm64) sh scripts/build-aarch64-full.sh ;;
    esac
fi

fifo="$PWD/$work/m250-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/$case_id.serial.log"
runner_log="$PWD/$work/m250-runner.log"
echo "[M250 NATIVE $label] genuine QEMU UART -> physical ext4 CTTY launcher -> original Debian glibc dash -i"
python3 scripts/m250-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"

log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch' "$log"
grep -Fq '[CMDLINE] userspace argv from Limine: naseed.argv=/sbin/m250-tty-launch --console' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/dash' "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M250 FAIL] guest serial has explicit failure' >&2
    exit 1
fi
sudo python3 scripts/m250-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest"
echo "[M250 INTERACTIVE ORIGINAL DEBIAN GLIBC EXT4 REAL $label QEMU PASS] unmodified dash -i, live UART, pipe, original coreutils, durable ext4 write, clean unmount and fsck"
