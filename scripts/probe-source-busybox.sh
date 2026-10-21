#!/bin/sh
# One batched acceptance for self-compiled, controlled-feature BusyBox on
# the actual shared Task/ELF/VM/FS kernel; reuse the existing hardware harness.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-source-busybox.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
binary="build/source-busybox/$port/busybox"
test -s "$binary" || sh scripts/build-source-busybox.sh "$port"
test -s "$binary" && test -s "build/source-busybox/$port/verified.config"
sh scripts/build-m11-vm-test.sh "$port"
sh scripts/build-m12-process-test.sh "$port"
extra_dir="build/source-busybox/$port/extra"
mkdir -p "$extra_dir"
cp "build/source-busybox/$port/m11-vm" "$extra_dir/m11-vm"
cp "build/source-busybox/$port/m12-process" "$extra_dir/m12-process"
export BUSYBOX_EXTRA_DIR="$extra_dir"
export BUSYBOX_PARITY_BINARY="$binary"
export BUSYBOX_PARITY_MANIFEST=scripts/source-busybox-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=8
case "$port" in
    amd64) sh scripts/probe-x86-busybox-parity.sh ;;
    riscv64) sh scripts/probe-riscv64-busybox-matrix.sh ;;
    arm64) sh scripts/probe-aarch64-busybox-matrix.sh ;;
    *) echo "unsupported target $port" >&2; exit 2 ;;
esac
echo "[SOURCE BUSYBOX MATRIX PASS] $port: 8/8 native self-compiled workloads"
