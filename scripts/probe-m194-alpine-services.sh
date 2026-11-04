#!/bin/sh
# M194: real Alpine 3.23.7 package DB, OpenRC cron service lifecycle, and
# persistent ext4 rootfs file across TWO physical system-QEMU boots.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m194-alpine-services.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

# Independent signed official M191 gate. No modification to its baseline
# disk, /etc/shadow or Alpine's original ELF/service scripts.
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

# Real login requires a deterministic credential only in an independent test
# image. Preserve packaged UID/GID/modes and all signed original executables.
overlay=$(mktemp -d "$PWD/$work/m194-service-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
disk="$PWD/$work/m194-service-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m194-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m194-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M194_DIAG_SECONDS:-150}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

fifo="$PWD/$work/m194-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
live_serial="$PWD/$logs/m194-batch.serial.log"
python3 scripts/m194-alpine-uart.py "$runner" "$live_serial" \
    "$PWD/$work/m194-service-runner.log" "$fifo"
# The first native VM was stopped ONLY AFTER verified live service/file data
# and sync; it did NOT cleanly unmount a still-running original PID1. Do not
# claim a clean ext4 volume before a separate successful second boot.
test -s "$live_serial"
cp "$live_serial" "$PWD/$work/m194-live.serial.log"
echo "[M194 ALPINE FIRST BOOT] $port original signed distro service+APK+file+sync; abrupt live-QEMU stop, no clean-unmount claim"

# Same disk, no recreate/truncate/reformat/copy. A new physical kernel runs
# the signed Alpine BusyBox as PID1 to read exact guest-created contents;
# normal PID1 exit 0 then executes lwext4_umount and device durable flush.
unset NAS_QEMU_SERIAL_FIFO
export BUSYBOX_PARITY_MANIFEST=scripts/m194-alpine-reopen-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M194_REOPEN_SECONDS:-160}
if ! sh "$runner" > "$PWD/$work/m194-reopen-runner.log" 2>&1; then
    echo "[M194 ALPINE REOPEN FAIL] $port second physical QEMU boot" >&2
    tail -120 "$PWD/$work/m194-reopen-runner.log" >&2
    test ! -f "$PWD/$logs/m194-batch.clean.log" || tail -120 "$PWD/$logs/m194-batch.clean.log" >&2
    exit 1
fi
reopen_log="$PWD/$logs/m194-batch.clean.log"
for exact in \
    'M194_DURABLE_REAL_EXT4_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[M191 ROOT] original Alpine PID1 from mounted ext4: /bin/busybox' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$exact" "$reopen_log" || {
        echo "[M194 ALPINE REOPEN FAIL] $port missing real second-boot evidence: $exact" >&2
        tail -150 "$reopen_log" >&2
        exit 1
    }
done
grep -Fq '[FAIL]' "$reopen_log" && {
    echo '[M194 ALPINE REOPEN FAIL] guest signaled failure' >&2
    exit 1
}

# Recheck partition, original signed package metadata and exact *new* file
# contents AFTER the guest has cleanly unmounted this very same disk.
sudo python3 scripts/m194-alpine-disk-check.py "$port" "$overlay" "$disk" \
    'M194_DURABLE_REAL_EXT4_2026'
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M194 ALPINE SERVICES PERSISTENCE PASS] $port real Alpine apk/OpenRC/crond, original ash, guest ext4 write+sync, second native-QEMU boot, exact host file readback and clean fsck"
