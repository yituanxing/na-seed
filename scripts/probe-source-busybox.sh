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
sh scripts/build-m13-init.sh "$port"
sh scripts/build-m14-groups.sh "$port"
sh scripts/build-m15-kill.sh "$port"
sh scripts/build-m16-stop.sh "$port"
sh scripts/build-m17-tty.sh "$port"
sh scripts/build-m18-ash-init.sh "$port"
extra_dir="build/source-busybox/$port/extra"
mkdir -p "$extra_dir"
cp "build/source-busybox/$port/m11-vm" "$extra_dir/m11-vm"
cp "build/source-busybox/$port/m12-process" "$extra_dir/m12-process"
cp "build/source-busybox/$port/m13-init" "$extra_dir/m13-init"
cp "build/source-busybox/$port/m14-groups" "$extra_dir/m14-groups"
cp "build/source-busybox/$port/m15-kill" "$extra_dir/m15-kill"
cp "build/source-busybox/$port/m16-stop" "$extra_dir/m16-stop"
cp "build/source-busybox/$port/m17-tty" "$extra_dir/m17-tty"
cp "build/source-busybox/$port/m18-ash-init" "$extra_dir/m18-ash-init"
if [ "$port" = amd64 ]; then
    cp "build/source-busybox/$port/m17-input" "$extra_dir/m17-input"
fi
export BUSYBOX_EXTRA_DIR="$extra_dir"
export BUSYBOX_PARITY_BINARY="$binary"
export BUSYBOX_PARITY_MANIFEST=scripts/source-busybox-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=13
case "$port" in
    amd64) sh scripts/probe-x86-busybox-parity.sh ;;
    riscv64) sh scripts/probe-riscv64-busybox-matrix.sh ;;
    arm64) sh scripts/probe-aarch64-busybox-matrix.sh ;;
    *) echo "unsupported target $port" >&2; exit 2 ;;
esac
if [ "$port" = amd64 ]; then
    sh scripts/probe-m17-interactive.sh
    sh scripts/probe-m18-ash-interactive.sh
else
    # Real firmware and PL011/SBI input are validated only on an actual
    # target machine, not under Linux QEMU-user or synthetic UART logs.
    sh scripts/probe-m18-port-interactive.sh "$port"
fi
echo "[SOURCE BUSYBOX MATRIX PASS] $port: 13/13 native self-compiled workloads"
