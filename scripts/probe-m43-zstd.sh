#!/bin/sh
# Genuine Linux distro ELF runtime gate, common on all three target ISAs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=zstd sh rootfs/alpine/build.sh "$port"
work="build/external/m43-zstd/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m43-zstd-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-1}
sh "$runner"
log="$logs/m43-zstd.clean.log"
test -s "$log"
for marker in M43_NATIVE_ZSTD_PASS M43_THREADS_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M43 FAIL] missing $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    }
done
# A serial marker emitted by kernel only for an actual CLONE_THREAD event.
grep -Fq '[THREAD] clone tid=' "$log" || {
    echo "[M43 FAIL] no real clone thread ($port)" >&2
    tail -120 "$log" >&2
    exit 1
}
echo "[M43 PASS] $port original zstd pthreads and shared task state"
