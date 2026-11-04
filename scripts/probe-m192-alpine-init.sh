#!/bin/sh
# M192 first-contact diagnostic: run the real Alpine /sbin/init as PID1
# without changing the established M191 exit-0 acceptance oracle.
# The result is ONLY evidence of native ELF entry, NOT OpenRC readiness.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m192-alpine-init.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64|riscv64|arm64) ;;
    *) echo '[M192 INIT DIAG FAIL] unsupported target' >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

# The exact same official Alpine bytes and physical root MUST first pass
# M191, including ext4 flush/fsck, before we investigate its long-lived init.
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
disk="$PWD/$work/alpine-root.img"
root="$work/rootfs"
busybox="$PWD/$work/smoke/elf/busybox"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
test -s "$busybox" && test -s "$rescue" && test -s "$disk"
# Unlike M191, genuine init is expected to stay alive. The QEMU diagnostic
# must eventually stop it, which is an unclean shutdown; run the probe on a
# disposable COPY so neither the verified baseline nor future fsck oracles
# depend on abrupt guest power loss.
probe_disk="$PWD/$work/m192-init-disposable.img"
cp "$disk" "$probe_disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1
export NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$probe_disk"
export NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m192-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m192-init-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
# This is a bounded diagnostic window, not a success-by-timeout mechanism.
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M192_DIAG_SECONDS:-65}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

runner_output="$work/m192-init-runner.log"
# The legacy runner correctly rejects a persistent PID1 (no exit-0 signal).
# Its exit code is not an M192 PASS or FAIL oracle. Keep it unchanged and
# diagnose the actual serial bytes instead of treating a timeout as success.
runner_rc=0
sh "$runner" > "$runner_output" 2>&1 || runner_rc=$?
log="$logs/m192-batch.clean.log"
test -s "$log" || {
    echo "[M192 INIT DIAG FAIL] $port has no physical QEMU serial evidence" >&2
    tail -90 "$runner_output" >&2
    exit 1
}
for marker in \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init'; do
    if ! grep -Fxq "$marker" "$log"; then
        echo "[M192 INIT DIAG FAIL] $port missing $marker" >&2
        tail -150 "$log" >&2
        exit 1
    fi
done
if ! grep -Fq '[ELF] userspace entry=' "$log" ||
   grep -Eq '\[FAIL\]|\[M191 ROOT FAIL\]|\[EXIT\] user status=|\[PASS\] cmdline userspace exit 0' "$log"; then
    echo "[M192 INIT DIAG FAIL] $port did not reach a live original init ELF" >&2
    tail -150 "$log" >&2
    exit 1
fi

# Distinguish genuine init launch from services/tty readiness. A future
# OpenRC-positive gate must inspect service state and live PID1 explicitly.
echo "[M192 INIT ENTRY OBSERVED] $port original /sbin/init mapped from real Alpine ext4 root; runner_rc=$runner_rc; NOT an OpenRC or login PASS"
tail -85 "$log"
# No clean unmount is claimed while PID1 is still alive; QEMU was stopped by
# the bounded runner. Verify the original, untouched M191 disk and its signed
# source bytes; do not misreport a dirty disposable live disk as corruption.
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$disk"
