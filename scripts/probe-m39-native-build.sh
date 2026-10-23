#!/bin/sh
# Run actual two-project GCC/Make/ar/Linux libc build with no guest helper ELF.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
  amd64) runner=scripts/probe-x86-busybox-parity.sh
         logs=build/busybox-source-parity-x86 ;;
  riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh
         logs=build/port-riscv64/full/matrix-source ;;
  arm64) runner=scripts/probe-aarch64-busybox-matrix.sh
         logs=build/port-aarch64/full/matrix-source ;;
  *) exit 2 ;;
esac
port=$1
# Package-managed original Alpine GCC/musl Make/binutils (same as M38).
NAS_STAGE_M39=1 sh rootfs/alpine/build.sh "$port"
work="build/external/m38-gcc/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/usr/bin/gcc"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m39-native-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=768
# Actual logs available on error; don't enable costly full syscall trace
# unless investigating a concrete failing syscall.
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m39-upstream.clean.log"
test -s "$log"
for marker in M39_ZLIB_PASS lua-vm-ok lua-io-ok M39_LUA_PASS M39_BUILD_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M39 FAIL] missing guest stage: $marker ($port)" >&2
        tail -150 "$log" >&2
        exit 1
    }
done
echo "[M39 PASS] $port original upstream zlib+Lua built by GCC/Make/ar INSIDE NaSeed"
