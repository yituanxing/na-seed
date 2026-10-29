#!/bin/sh
# Native Linux x86 oracle + original source-BusyBox system-QEMU on one ISA.
# Usage: sh scripts/test-m104-probe.sh amd64|riscv64|arm64
# Requires the existing probe-source-busybox.sh <port> build on this CI job.
set -eu
if [ "$#" -ne 1 ]; then
    echo 'usage: test-m104-probe.sh amd64|riscv64|arm64' >&2
    exit 2
fi
port=$1
case "$port" in
  amd64)
    target=x86_64-linux-musl
    machine='Advanced Micro Devices X86-64'
    harness=scripts/probe-x86-busybox-parity.sh
    log=build/busybox-source-parity-x86/m104-exception.clean.log ;;
  riscv64)
    target=riscv64-linux-musl
    machine='RISC-V'
    harness=scripts/probe-riscv64-busybox-matrix.sh
    log=build/port-riscv64/full/matrix-source/m104-exception.clean.log ;;
  arm64)
    target=aarch64-linux-musl
    machine='AArch64'
    harness=scripts/probe-aarch64-busybox-matrix.sh
    log=build/port-aarch64/full/matrix-source/m104-exception.clean.log ;;
  *) echo "[M104 FAIL] unsupported port $port" >&2; exit 2 ;;
esac
binary="build/source-busybox/$port/busybox"
extra="build/source-busybox/$port/extra"
test -s "$binary" && test -d "$extra" || {
    echo "[M104 FAIL] first run probe-source-busybox.sh $port" >&2
    exit 1
}
command -v clang >/dev/null 2>&1
command -v ld.lld >/dev/null 2>&1
command -v readelf >/dev/null 2>&1
guest="$extra/m104-exception"
clang --target="$target" -O2 -std=c11 -Wall -Wextra -Werror \
    -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
    -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
    -Wl,-e,_start -Wl,-z,max-page-size=4096 \
    scripts/test-m104-native-exception.c -o "$guest"
readelf -h "$guest" | grep -Fq "$machine"
if readelf -l "$guest" | grep -Fq 'INTERP'; then
    echo "[M104 FAIL] PT_INTERP in standalone guest ELF ($port)" >&2
    exit 1
fi
if [ "$port" = amd64 ]; then
    # Same x86 binary, no host substitute/relink; raw Linux syscall oracle.
    (ulimit -c 0; timeout 30s "$guest") > \
        "build/source-busybox/$port/m104-linux-oracle.log"
    grep -Fxq M104_NATIVE_EXCEPTION_PASS \
        "build/source-busybox/$port/m104-linux-oracle.log"
    echo '[M104 LINUX ORACLE PASS] exact native Linux x86 guest ELF'
fi
export BUSYBOX_EXTRA_DIR="$extra"
export BUSYBOX_PARITY_BINARY="$binary"
export BUSYBOX_PARITY_MANIFEST=scripts/test-m104-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=192
export NAS_SYSCALL_TRACE=0
sh "$harness"
test -s "$log"
for marker in M104_SIGILL_PASS M104_SIGTRAP_PASS M104_SIGSEGV_PASS \
              M104_POST_FAULT_FORK_PASS M104_NATIVE_EXCEPTION_PASS; do
    if ! grep -Fxq "$marker" "$log"; then
        echo "[M104 QEMU FAIL] absent guest marker $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    fi
done
if [ "$port" = amd64 ]; then
    grep -Fxq M104_SIGFPE_PASS "$log"
fi
grep -Fq '[PROC] wait4 reaped pid=' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo "[M104 QEMU FAIL] actual kernel failure ($port)" >&2
    exit 1
fi
echo "[M104 NATIVE QEMU PASS] $port real user exceptions, child signal wait4, later fork healthy"
