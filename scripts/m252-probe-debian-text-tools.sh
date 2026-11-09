#!/bin/sh
# M252: original signed Debian 13/glibc grep/sed/find, executed interactively from
# a real writable VirtIO ext4 root on native NaSeed system-QEMU.
set -eu

[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/m252-probe-debian-text-tools.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64) triplet=x86_64-linux-gnu; loader=ld-linux-x86-64.so.2; label=X86; cc=gcc ;;
    riscv64) triplet=riscv64-linux-gnu; loader=ld-linux-riscv64-lp64d.so.1; label=RV64; cc=riscv64-linux-gnu-gcc ;;
    arm64) triplet=aarch64-linux-gnu; loader=ld-linux-aarch64.so.1; label=ARM64; cc=aarch64-linux-gnu-gcc ;;
    *) echo "[M252 FAIL] unsupported architecture $port" >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

for tool in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum \
    mke2fs e2fsck debugfs sudo xorriso sgdisk mformat mmd mcopy "$cc"; do
    command -v "$tool" >/dev/null || { echo "[M252 FAIL] missing $tool" >&2; exit 1; }
done

work="build/m252-debian-text-tools/$port"
root="$work/rootfs"
disk="$PWD/$work/m252-debian-root.img"
manifest="$work/source/manifest.json"
m251extra="$work/m251-extra.json"
extra="$work/m252-extra.json"
mkdir -p "$work"
python3 scripts/m248-fetch-debian-multiarch.py --arch "$port" --out "$work/source"
source=$(cd "$work/source/sysroot" && pwd -P)
test -s "$manifest"

# Retain M249/M250's exact original Debian base layout; extra M252 ELF and
# their closed, authenticated dependencies are added by the independent
# signed-source provenance verifier, not built or substituted on the host.
mkdir -p "$root/usr/bin" "$root/usr/lib/$triplet" "$root/lib" "$root/lib64" \
    "$root/sbin" "$root/etc" "$root/dev" "$root/proc" "$root/sys" \
    "$root/var/tmp" "$root/tmp"
chmod 1777 "$root/tmp" "$root/var/tmp"
for name in dash true cat wc printf; do
    original="$source/usr/bin/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M252 FAIL] signed source path escape' >&2; exit 1 ;; esac
    cp -L "$original" "$root/usr/bin/$name"
    chmod 755 "$root/usr/bin/$name"
    cmp "$original" "$root/usr/bin/$name"
done
for name in libc.so.6 "$loader"; do
    original="$source/usr/lib/$triplet/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M252 FAIL] signed glibc path escape' >&2; exit 1 ;; esac
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

# Keep the M249/M250 signed disk base contracts independent of the new batch.
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"
printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
printf '%s\n' M250-EXT4-INPUT > "$root/etc/m250-input"
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m251-init.sh"
chmod 755 "$root/etc/m251-init.sh"
printf '%s\n' M251-EXT4-INPUT > "$root/etc/m251-input"
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m252-init.sh"
chmod 755 "$root/etc/m252-init.sh"
printf '%s\n' M252-EXT4-INPUT > "$root/etc/m252-input"

"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m250-debian-tty-launch.c -o "$root/sbin/m250-tty-launch"
chmod 755 "$root/sbin/m250-tty-launch"
if readelf -lW "$root/sbin/m250-tty-launch" | grep -q INTERP; then
    echo '[M252 FAIL] test-only CTTY launcher must be static' >&2; exit 1
fi

python3 scripts/m251-debian-provenance.py prepare "$port" "$root" "$manifest" "$m251extra"
python3 scripts/m252-debian-provenance.py prepare "$port" "$root" "$manifest" "$m251extra" "$extra"
sudo python3 scripts/m252-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
sudo chown "$(id -u):$(id -g)" "$disk"
test -r "$disk" && test -w "$disk" || {
    echo '[M252 FAIL] real ext4 disk inaccessible to unprivileged QEMU' >&2
    exit 1
}

rescue="$work/rescue"
mkdir -p "$rescue"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/rescue-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/rescue-busybox" "$rescue"
test -s "$rescue/rootfs.cpio"
if cpio -it --quiet < "$rescue/rootfs.cpio" | grep -Eq \
    '(^|/)(sbin/m250-tty-launch|usr/bin/(dash|true|cat|wc|printf|ls|head|sort|mkdir|rm|env|sleep|grep|sed|find))$'; then
    echo '[M252 FAIL] original Debian ELF leaked into rescue CPIO' >&2
    exit 1
fi

case_id=m252-batch
case_file="$work/m252-cases.tsv"
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
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M252_QEMU_TIMEOUT:-300}
export NAS_SYSCALL_TRACE=1

if [ "$port" = amd64 ]; then
    . scripts/native-source-qemu.sh
    nas_source_build_kernel m252-debian
else
    case "$port" in
        riscv64) sh scripts/build-riscv64-full.sh ;;
        arm64) sh scripts/build-aarch64-full.sh ;;
    esac
fi

fifo="$PWD/$work/m252-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/$case_id.serial.log"
runner_log="$PWD/$work/m252-runner.log"
echo "[M252 NATIVE $label] authenticated original Debian grep/sed/find over real UART/ext4"
python3 scripts/m252-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"

log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/dash' "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
for name in grep sed find; do
    grep -Fq "[EXEC] replaced process with lwext4 ELF: /usr/bin/$name" "$log"
done
if grep -Fq '[FAIL]' "$log"; then
    echo '[M252 FAIL] native QEMU serial has explicit failure' >&2; exit 1
fi
sudo python3 scripts/m252-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
echo "[M252 ORIGINAL DEBIAN TEXT TOOLS EXT4 REAL $label QEMU PASS] three new signed ELF, live UART commands, clean fsck and durable disk readback"
