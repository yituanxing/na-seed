#!/bin/sh
# An OS compatibility gate: test official prebuilt binaries. Never compile
# source under NaSeed. Reuse original three-ISA boot & Linux ABI harness.
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
NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh "$port"
work="build/external/m39-packages/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m39-packages-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=512M
export NAS_ACCEPTANCE_DISK_MB=256
# Keep runtime checks responsive; ordinary dynamic userspace should not
# have compiler-build budgets or noisy syscall tracing enabled by default.
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m39-packages.clean.log"
test -s "$log"
for marker in lua-vm-ok lua-io-ok M39_LUA_PASS M39_JQ_PASS \
              M39_SQLITE_PASS M39_PACKAGES_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M39 FAIL] missing guest marker $marker ($port)" >&2
        tail -130 "$log" >&2
        exit 1
    }
done
echo "[M39 PASS] $port prebuilt Alpine Lua/jq/SQLite real user programs"
