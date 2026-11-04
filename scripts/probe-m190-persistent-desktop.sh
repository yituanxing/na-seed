#!/bin/sh
# Two MORE genuine QEMU boots on the same previously verified M180 raw disk.
# Do not call M178 prepare, truncate, format, or create a second rootfs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
work="build/external/m174-software-batch/$port"
disk="$PWD/build/m178/async-blk/$port/independent-virtio-blk.img"
test -s "$disk" && test -s "$work/rootfs.cpio" || {
    echo '[M190 FAIL] real prior M180 disk and signed CPIO required' >&2
    exit 1
}
export NAS_ENABLE_M179_FS_IRQ=1 NAS_ENABLE_M180_FS_USER_IRQ=1
export NAS_ENABLE_VIRTIO_BLK_ASYNC=1 NAS_ENABLE_LWEXT4=1
export NAS_QEMU_ASYNC_BLK=1 NAS_QEMU_ASYNC_BLK_IMAGE="$disk"
export NAS_QEMU_INPUT_OPTIN=1 NAS_ENABLE_VIRTIO_INPUT=1
export NAS_QEMU_VIRTIO_GPU=1 NAS_ENABLE_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=850 BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m187-vt100-batch-cases.tsv
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
for phase in write reopen; do
    export NAS_QEMU_QMP_SOCKET="$work/m190-$phase.qmp"
    log="$logs/m190-$phase.clean.log"
    serial="$logs/m190-$phase.serial.log"
    oracle="$work/m190-$phase-qmp-check.log"
    mkdir -p "$logs"
    truncate -s 0 "$serial"
    python3 scripts/m187-qmp-verify.py --serial "$serial" \
        --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" \
        --timeout 850 --m188 --m189 "--m190-$phase" > "$oracle" 2>&1 &
    watcher=$!
    if ! sh "$runner"; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
        echo "[M190 GUEST FAIL] $port $phase" >&2
        test ! -s "$log" || tail -120 "$log" >&2
        test ! -s "$oracle" || tail -120 "$oracle" >&2
        exit 1
    fi
    if ! wait "$watcher"; then
        echo "[M190 QMP FAIL] $port $phase" >&2
        tail -120 "$oracle" >&2
        exit 1
    fi
    grep -Fq "[M190 QMP $phase PASS]" "$oracle"
    [ "$(grep -Fxc M190_DURABLE_SAVED_FUNCTION_PASS "$log" || :)" -eq 1 ]
    grep -Fq '[M180 FS USER IRQ] clean unmount and durable flush' "$log"
    grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/vim' "$log"
    grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/nano' "$log"
    python3 scripts/m190-durable-fixture.py verify "$disk"
    echo "[M190 $phase NATIVE PASS] $port same physical disk, real GUI and IRQ"
done
echo "[M190 DURABLE DESKTOP REBOOT PASS] $port actual Vim/Nano ext4, same raw disk reboot, host readback and clean fsck"
