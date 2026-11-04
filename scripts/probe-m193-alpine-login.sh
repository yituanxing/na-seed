#!/bin/sh
# M193: real original Alpine root login and command execution over QEMU UART.
# M191 still independently proves a clean physical disk and original ELF bytes.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m193-alpine-login.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64|riscv64|arm64) ;;
    *) echo '[M193 ALPINE LOGIN FAIL] unsupported ISA' >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

# Do not weaken or skip the already-green original M191 exit-0 proof.
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

# Stage a new independent ext4 disk.  No root credential or securetty change
# is applied to the signed upstream rootfs or M191's clean physical baseline.
# The runtime test alone has a deterministic CI-only password.
overlay=$(mktemp -d "$PWD/$work/m193-auth-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
probe_disk="$PWD/$work/m193-login-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$probe_disk"
sudo chown "$(id -u):$(id -g)" "$probe_disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$probe_disk"
export NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m193-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m193-login-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M193_DIAG_SECONDS:-110}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

fifo="$PWD/$work/m193-serial-input.fifo"
runner_log="$PWD/$work/m193-login-runner.log"
log="$PWD/$logs/m193-batch.serial.log"
python3 scripts/m193-alpine-uart.py "$runner" "$log" "$runner_log" "$fifo"

# A live shell is deliberately terminated by the host after witnessing actual
# command outputs.  The disposable filesystem is therefore *not* fsck-clean
# evidence.  Reverify only the untouched official M191 baseline.
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M193 ALPINE LOGIN VERIFIED] $port untouched signed M191 ext4 baseline, original distro executables, real serial root authentication, ash and /bin/sh"
