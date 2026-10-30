#!/bin/sh
# Fail-closed M105: compile the real Linux ABI workload, stage into the
# *existing upstream BusyBox* initramfs, and boot the actual NaSeed kernel.
# This is one independent native firmware/QEMU boot per target ISA.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m105-signal-group.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64) work=build/busybox-source-parity-x86 ;;
    riscv64) work=build/port-riscv64/full/matrix-source ;;
    arm64) work=build/port-aarch64/full/matrix-source ;;
    *) echo "[M105 FAIL] unknown target ISA: $port" >&2;exit 2 ;;
esac
base="build/source-busybox/$port"
source_binary="$base/busybox"
extra_dir="$base/extra"
test -s "$source_binary" && test -d "$extra_dir" || {
    echo "[M105 FAIL] first run sh scripts/probe-source-busybox.sh $port" >&2
    exit 1
}
sh scripts/build-m105-signal-group.sh "$port"
cp "$base/m105-signal-group" "$extra_dir/m105-signal-group"
BUSYBOX_EXTRA_DIR="$extra_dir" \
    sh scripts/stage-busybox-rootfs.sh "$source_binary" "$work"
test -s "$work/rootfs.cpio"
if [ "$port" = amd64 ];then
    NASEED_INTERACTIVE_INIT=/bin/m105-signal-group \
    NASEED_INTERACTIVE_DRIVER=scripts/m105-signal-group-serial-check.py \
        sh scripts/probe-m18-ash-interactive.sh
else
    NASEED_INTERACTIVE_INIT=/bin/m105-signal-group \
    NASEED_INTERACTIVE_DRIVER=scripts/m105-signal-group-serial-check.py \
        sh scripts/probe-m18-port-interactive.sh "$port"
fi
echo "[M105 GROUP NATIVE PASS] $port real three-ISA thread-group TTY guest"
