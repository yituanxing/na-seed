#!/bin/sh
# One genuine signed Alpine /usr/bin/ncat TCP loopback, native x86-64 QEMU.
# This delegates to the existing M141 APK staging and BusyBox QEMU runner;
# no historical M141 full-batch or multi-ISA regression is involved.
set -eu
[ "$#" -eq 0 ] || {
    echo 'usage: sh scripts/probe-lwip-ncat-smoke.sh' >&2
    exit 2
}

export NAS_ENABLE_LWIP=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-ncat-cases.tsv

# Preserve signed APK provenance and execute the unchanged original ELF.
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/ncat"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"

sh scripts/probe-x86-busybox-parity.sh
echo '[LWIP NCAT NATIVE QEMU PASS] amd64 signed Alpine ncat loopback'
