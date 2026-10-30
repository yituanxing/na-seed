#!/bin/sh
# Genuine Linux distro ELF runtime gate, common on all three target ISAs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=pthread sh rootfs/alpine/build.sh "$port"
sh scripts/stage-m42-abi-oracles.sh "$port"
work="build/external/m42-pthread/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m42-pthread-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m42-pthread.clean.log"
test -s "$log"
for marker in M42_XZ_THREADS_PASS M42_PTHREAD_PASS \
    A009_LINUX_PWRITE_APPEND_POSITION_PASS \
    A010_LINUX_NATIVE_FCNTL_FLAGS_PASS \
    A009_A010_NATIVE_ABI_DIFFERENTIAL_PASS \
    A014_NATIVE_BLOCKED_READV_IOV_SNAPSHOT_PASS \
    A014_NATIVE_BLOCKED_READV_FILE_SNAPSHOT_PASS \
    A014_NATIVE_TWO_BLOCKING_OWNERSHIP_RACES_3X_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M42 FAIL] missing $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    }
done
# A serial marker emitted by kernel only for an actual CLONE_THREAD event.
grep -Fq '[THREAD] clone tid=' "$log" || {
    echo "[M42 FAIL] no real clone thread ($port)" >&2
    tail -120 "$log" >&2
    exit 1
}
echo "[M42+A014 PASS] $port original pthread XZ and real guest blocked readv ownership"
