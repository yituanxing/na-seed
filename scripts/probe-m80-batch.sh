#!/bin/sh
# One real NaSeed QEMU boot per ISA for a batch of original Alpine ELF tools.
# Keep signed APK staging, guest execution and serial evidence separate.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m80-batch.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m80-prebuilt.sh "$port"
work="build/external/m80-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m80-batch-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=0
log="$logs/m80-batch.clean.log"
if ! sh "$runner"; then
  echo "[M80 FAIL] $port genuine original Alpine program batch" >&2
  test ! -s "$log" || tail -360 "$log" >&2
  exit 1
fi
test -s "$log"
for marker in M80_GREP_PASS M80_SED_PASS M80_GAWK_PASS \
              M80_FIND_PASS M80_COREUTILS_PASS M80_JQ_PASS \
              M80_RIPGREP_PASS M80_FD_PASS M80_TREE_PASS \
              M80_TAR_PASS M80_GZIP_PASS M80_XZ_PASS \
              M80_BZIP2_PASS M80_ZIP_PASS M80_UNZIP_PASS \
              M80_ZSTD_PASS M80_SQLITE_PASS M80_BC_PASS \
              M80_RSYNC_PASS \
              M80_TEXT_TREE_PASS M80_ARCHIVES_PASS \
              M80_SQLITE_BC_PASS \
              M80_NANO_DEFERRED M80_BATCH_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M80 FAIL] $port missing genuine guest assertion: $marker" >&2
    tail -360 "$log" >&2
    exit 1
  fi
done
echo "[M80 PASS] $port original Alpine 19-program functional batch (nano interactive deferred)"
