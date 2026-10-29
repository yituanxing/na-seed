#!/bin/sh
# Run unchanged real Debian BusyBox 1.37.0 as a Linux host oracle (amd64),
# then via the established native QEMU harness. No copied/faked PASS markers.
set -eu
test "$#" -eq 1 || {
  echo 'usage: probe-vfs-path-first-busybox.sh amd64|riscv64|arm64' >&2
  exit 2
}
arch=$1
case "$arch" in
  amd64) harness=scripts/probe-x86-busybox-parity.sh ;;
  riscv64) harness=scripts/probe-riscv64-busybox-matrix.sh ;;
  arm64) harness=scripts/probe-aarch64-busybox-matrix.sh ;;
  *) exit 2 ;;
esac
work="build/path-first-busybox/$arch"
mkdir -p "$work/extra"
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/busybox"
cp tests/vfs-path-first-busybox-workload \
   "$work/extra/vfs-path-first-busybox-workload"
chmod 755 "$work/extra/vfs-path-first-busybox-workload"
if [ "$arch" = amd64 ]; then
  mkdir -p "$work/linux-a"
  NAS_APP_BUSYBOX="$(pwd)/$work/busybox" \
  NAS_APP_FS_A="$(pwd)/$work/linux-a" \
    sh tests/vfs-path-first-busybox-workload > "$work/linux-oracle.log"
  grep -Fxq '[PASS] REAL PATH FIRST APP original Debian BusyBox: cwd rename, held FD, mount .., symlink trailing slash' \
    "$work/linux-oracle.log"
  echo '[LINUX PATH FIRST BUSYBOX ORACLE PASS] unchanged original program and filesystem workload'
fi
export BUSYBOX_PARITY_BINARY="$work/busybox"
export BUSYBOX_EXTRA_DIR="$work/extra"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-path-first-cases.tsv
export BUSYBOX_PARITY_CASE=path-first
sh "$harness"
echo "[PATH FIRST NATIVE QEMU PASS] $arch original Debian BusyBox applets, tmpfs/procfs mount crossing"
