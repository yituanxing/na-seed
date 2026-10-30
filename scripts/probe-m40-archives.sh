#!/bin/sh
# Genuine Linux distro ELF runtime gate, common on all three target ISAs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=archive sh rootfs/alpine/build.sh "$port"
work="build/external/m40-archive/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m40-archives-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=512M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m40-archives.clean.log"
test -s "$log"
for marker in M40_TAR_CREATE_PASS M40_XZ_PASS \
              M40_TAR_EXTRACT_PASS M40_ARCHIVES_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M40 FAIL] missing $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    }
done
echo "[M40 PASS] $port original Alpine GNU tar + XZ Utils archive lifecycle"
