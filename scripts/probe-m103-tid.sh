#!/bin/sh
# M103: one REAL native system-QEMU guest per ISA, reusing the original
# source-BusyBox ELF, prebuilt Limine boot chain and existing parity harness.
# Intended to run immediately AFTER probe-source-busybox.sh on the same CI
# job: no second BusyBox/source-toolchain build or Alpine package download.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: probe-m103-tid.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
  amd64)
    target=x86_64-linux-musl
    machine='Advanced Micro Devices X86-64'
    harness=scripts/probe-x86-busybox-parity.sh
    log=build/busybox-source-parity-x86/m103-tid.clean.log ;;
  riscv64)
    target=riscv64-linux-musl
    machine='RISC-V'
    harness=scripts/probe-riscv64-busybox-matrix.sh
    log=build/port-riscv64/full/matrix-source/m103-tid.clean.log ;;
  arm64)
    target=aarch64-linux-musl
    machine='AArch64'
    harness=scripts/probe-aarch64-busybox-matrix.sh
    log=build/port-aarch64/full/matrix-source/m103-tid.clean.log ;;
  *) echo "[M103 FAIL] unknown ISA $port" >&2; exit 2 ;;
esac
binary="build/source-busybox/$port/busybox"
extra="build/source-busybox/$port/extra"
test -s "$binary" && test -d "$extra" || {
    echo "[M103 FAIL] run probe-source-busybox.sh $port first" >&2
    exit 1
}
command -v clang >/dev/null 2>&1
command -v ld.lld >/dev/null 2>&1
command -v readelf >/dev/null 2>&1
guest="$extra/m103-tid"
clang --target="$target" -O2 -std=c11 -Wall -Wextra -Werror \
    -ffreestanding -nostdinc -fno-stack-protector -fno-builtin \
    -fno-pic -fno-pie -nostdlib -static -fuse-ld=lld \
    -Wl,-e,_start -Wl,-z,max-page-size=4096 \
    scripts/test-m103-native-tid-abi.c -o "$guest"
test -s "$guest"
readelf -h "$guest" | grep -Fq "$machine"
if readelf -l "$guest" | grep -Fq 'INTERP'; then
    echo "[M103 FAIL] guest ELF unexpectedly uses dynamic interpreter ($port)" >&2
    exit 1
fi
# Independent native Linux oracle executes the EXACT same ELF (not a
# relinked host substitute) before the x86 system QEMU acceptance.
if [ "$port" = amd64 ]; then
    # A guest regression that deadlocks on host Linux is a failed oracle,
    # not an unbounded CI stall. The system-QEMU path has its own timeout.
    timeout 60s "$guest" > "build/source-busybox/$port/m103-linux-oracle.log"
    grep -Fxq M103_NATIVE_ABI_PASS \
        "build/source-busybox/$port/m103-linux-oracle.log"
    echo '[M103 LINUX ORACLE PASS] target ELF via raw native Linux syscalls'
fi
export BUSYBOX_EXTRA_DIR="$extra"
export BUSYBOX_PARITY_BINARY="$binary"
export BUSYBOX_PARITY_MANIFEST=scripts/m103-native-tid-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=192
export NAS_SYSCALL_TRACE=0
sh "$harness"
test -s "$log"
for marker in M103_FORK_CHILD_TID_PASS M103_THREAD_CLEAR_ONLY_PASS \
              M103_INVALID_CLEAR_TID_PASS M103_ROBUST_PENDING_WAKE_PASS \
              M103_NATIVE_ABI_PASS; do
    if ! grep -Fxq "$marker" "$log"; then
        echo "[M103 QEMU FAIL] missing actual guest marker $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    fi
done
grep -Fq '[PROC] fork parent=' "$log"
grep -Fq '[THREAD] clone tid=' "$log"
grep -Fq '[PROC] wait4 reaped pid=' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo "[M103 QEMU FAIL] kernel failure on serial ($port)" >&2
    exit 1
fi
echo "[M103 NATIVE QEMU PASS] $port real CHILD_SETTID/COW, musl CLEAR-only, invalid exit pointer, pending robust wake"
