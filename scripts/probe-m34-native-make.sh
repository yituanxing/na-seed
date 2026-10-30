#!/bin/sh
# Demand end-to-end original program behaviour, same source and contract
# across three full NaSeed kernels. This is not a build-only smoke test.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
# Linux QEMU-user oracle is distinct from the real NaSeed system-QEMU runner.
case "$port" in
  amd64) emulator='' ;;
  riscv64) emulator=qemu-riscv64 ;;
  arm64) emulator=qemu-aarch64 ;;
  *) exit 2 ;;
esac
# M33 Linux-native compiler oracle has already passed in this shard.
test -s "build/external/m33-tcc/$port/tcc"
sh scripts/build-m34-native-gnumake.sh "$port"
work="build/external/m34-gnumake/$port"
# Independently test the *same* native TinyCC linkage and executable under
# actual Linux (QEMU-user on cross targets), before guest diagnosis.
compiler="build/external/m33-tcc/$port/tcc"
output="$work/native-hello"
if [ -n "$emulator" ]; then
  "$emulator" "$compiler" -nostdlib -static -o "$output" \
    tests/m34-portable-hello.c
else
  "$compiler" -nostdlib -static -o "$output" tests/m34-portable-hello.c
fi
readelf -h "$output" | grep -q 'EXEC (Executable file)'
if readelf -l "$output" | grep -q INTERP; then
  echo '[M34 LINUX FAIL] linked ELF requests dynamic interpreter' >&2
  exit 1
fi
if [ -n "$emulator" ]; then
  "$emulator" "$output" > "$work/linux-generated-exec.log"
else
  "$output" > "$work/linux-generated-exec.log"
fi
grep -Fxq naseed-three-isa-build-exec "$work/linux-generated-exec.log"
echo "[M34 LINUX PASS] $port genuine TinyCC linked ELF executed on Linux"
sh scripts/stage-m34-native-make-rootfs.sh "$port"
export BUSYBOX_PARITY_BINARY="$work/gmake"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m34-native-make-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
sh "$runner"
echo "[M34 NASEED PASS] $port original GNU Make invoked TinyCC linking and executing new tmpfs ELF"
