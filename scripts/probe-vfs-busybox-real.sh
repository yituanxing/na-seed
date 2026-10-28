#!/bin/sh
# One authentic Debian BusyBox package and one real POSIX shell script on
# x86-64, RV64 and AArch64. Reuse existing native parity/QEMU harnesses;
# never synthesize a successful log or replace real applet binaries.
set -eu
test "$#" -eq 1 || {
  echo 'usage: probe-vfs-busybox-real.sh amd64|riscv64|arm64' >&2
  exit 2
}
arch=$1
case "$arch" in
  amd64) harness=scripts/probe-x86-busybox-parity.sh ;;
  riscv64) harness=scripts/probe-riscv64-busybox-matrix.sh ;;
  arm64) harness=scripts/probe-aarch64-busybox-matrix.sh ;;
  *) exit 2 ;;
esac
work="build/real-vfs-busybox/$arch"
mkdir -p "$work/extra"
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/original-busybox"
cp tests/vfs-busybox-real-workload "$work/extra/vfs-busybox-real-workload"
chmod 755 "$work/extra/vfs-busybox-real-workload"
# Linux oracle runs the EXACT same unmodified Debian BusyBox binary and
# script on two independently created directories. Guest uses identical
# program inputs, with VFS mounts as the only controlled difference.
if [ "$arch" = amd64 ]; then
    mkdir -p "$work/linux-oracle-a" "$work/linux-oracle-b"
    NAS_APP_BUSYBOX="$(pwd)/$work/original-busybox" \
    NAS_APP_FS_A="$(pwd)/$work/linux-oracle-a" \
    NAS_APP_FS_B="$(pwd)/$work/linux-oracle-b" \
      sh tests/vfs-busybox-real-workload > "$work/linux-oracle.log"
    grep -Fxq '[PASS] REAL VFS APP original Debian BusyBox: tmpfs + independent RamFS cross-mount file workloads' "$work/linux-oracle.log"
    echo '[LINUX ORACLE PASS] original Debian BusyBox identical script and real host filesystems'
fi
# Same opaque kernel VFS pathname semantics; FS backend differs only by
# mount location. The test checks BusyBox exit + output, not canned stdout.
export BUSYBOX_PARITY_BINARY="$work/original-busybox"
export BUSYBOX_EXTRA_DIR="$work/extra"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-busybox-real-cases.tsv
export BUSYBOX_PARITY_CASE=real-vfs
export NAS_ENABLE_RAMFS=1
sh "$harness"
echo "[VFS REAL BUSYBOX THREE-ISA PROFILE PASS] $arch original Debian BusyBox through RamFS and tmpfs"
