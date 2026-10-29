#!/bin/sh
# M105 actual native CPU RW -> RX JIT, child COW page rewrite and parent
# executable isolation, reusing each ISA's original source BusyBox system-QEMU.
# Usage: sh scripts/test-m105-native-jit-probe.sh amd64|riscv64|arm64
set -eu
if [ "$#" -ne 1 ]; then
    echo 'usage: test-m105-native-jit-probe.sh amd64|riscv64|arm64' >&2
    exit 2
fi
port=$1
case "$port" in
  amd64)
    target=x86_64-linux-musl
    machine='Advanced Micro Devices X86-64'
    harness=scripts/probe-x86-busybox-parity.sh
    log=build/busybox-source-parity-x86/m105-native-jit.clean.log ;;
  riscv64)
    target=riscv64-linux-musl
    machine='RISC-V'
    harness=scripts/probe-riscv64-busybox-matrix.sh
    log=build/port-riscv64/full/matrix-source/m105-native-jit.clean.log ;;
  arm64)
    target=aarch64-linux-musl
    machine='AArch64'
    harness=scripts/probe-aarch64-busybox-matrix.sh
    log=build/port-aarch64/full/matrix-source/m105-native-jit.clean.log ;;
  *) echo "[M105 NATIVE JIT FAIL] unknown ISA $port" >&2; exit 2 ;;
esac
binary="build/source-busybox/$port/busybox"
extra="build/source-busybox/$port/extra"
test -s "$binary" && test -d "$extra" || {
    echo "[M105 NATIVE JIT FAIL] run probe-source-busybox.sh $port first" >&2
    exit 1
}
command -v clang >/dev/null 2>&1
command -v ld.lld >/dev/null 2>&1
command -v readelf >/dev/null 2>&1
guest="$extra/m105-native-jit"
clang --target="$target" -O2 -std=c11 -Wall -Wextra -Werror \
    -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
    -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
    -Wl,-e,_start -Wl,-z,max-page-size=4096 \
    scripts/test-m105-native-jit.c -o "$guest"
readelf -h "$guest" | grep -Fq "$machine"
if readelf -l "$guest" | grep -Fq 'INTERP'; then
    echo "[M105 NATIVE JIT FAIL] unexpectedly linked PT_INTERP ($port)" >&2
    exit 1
fi
if [ "$port" = amd64 ]; then
    oracle="build/source-busybox/$port/m105-native-jit-linux-oracle.log"
    if ! (ulimit -c 0; timeout 30s "$guest") > "$oracle"; then
        echo '[M105 LINUX ORACLE FAIL] guest exited nonzero' >&2
        tail -80 "$oracle" >&2
        exit 1
    fi
    grep -Fxq M105_JIT_MPROTECT_EXEC_PASS "$oracle"
    grep -Fxq M105_NATIVE_JIT_COW_PASS "$oracle"
    echo '[M105 LINUX ORACLE PASS] same freestanding x86 guest ELF'
fi
export BUSYBOX_EXTRA_DIR="$extra"
export BUSYBOX_PARITY_BINARY="$binary"
export BUSYBOX_PARITY_MANIFEST=scripts/test-m105-native-jit-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=192
export NAS_SYSCALL_TRACE=0
sh "$harness"
test -s "$log"
for marker in M105_JIT_MPROTECT_EXEC_PASS M105_NATIVE_JIT_COW_PASS; do
    if ! grep -Fxq "$marker" "$log"; then
        echo "[M105 NATIVE JIT QEMU FAIL] missing $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    fi
done
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo "[M105 NATIVE JIT QEMU FAIL] kernel emitted FAIL ($port)" >&2
    tail -140 "$log" >&2
    exit 1
fi
echo "[M105 NATIVE JIT QEMU PASS] $port real CPU JIT RW/RX and private post-fork instruction rewrite"
