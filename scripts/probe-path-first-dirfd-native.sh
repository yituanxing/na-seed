#!/bin/sh
# Build the identical Linux ELF for one ISA and boot it with the established
# source-BusyBox native QEMU harness; a mere ELF compile is NOT a guest pass.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-path-first-dirfd-native.sh amd64|riscv64|arm64' >&2;exit 2; }
port=$1
case "$port" in
  amd64) work=build/busybox-source-parity-x86 ;;
  riscv64) work=build/port-riscv64/full/matrix-source ;;
  arm64) work=build/port-aarch64/full/matrix-source ;;
  *) exit 2 ;;
esac
base="build/source-busybox/$port"
test -s "$base/busybox" && test -d "$base/extra" || {
  echo "[PATH FIRST DIRFD FAIL] first run probe-source-busybox.sh $port" >&2
  exit 1
}
sh scripts/build-path-first-dirfd-native.sh "$port"
cp "$base/path-first-dirfd-native" "$base/extra/path-first-dirfd-native"
BUSYBOX_EXTRA_DIR="$base/extra" \
  sh scripts/stage-busybox-rootfs.sh "$base/busybox" "$work"
test -s "$work/rootfs.cpio"
if [ "$port" = amd64 ];then
  NASEED_INTERACTIVE_INIT=/bin/path-first-dirfd-native \
  NASEED_INTERACTIVE_DRIVER=scripts/path-first-dirfd-serial-check.py \
    sh scripts/probe-m18-ash-interactive.sh
else
  NASEED_INTERACTIVE_INIT=/bin/path-first-dirfd-native \
  NASEED_INTERACTIVE_DRIVER=scripts/path-first-dirfd-serial-check.py \
    sh scripts/probe-m18-port-interactive.sh "$port"
fi
echo "[PATH FIRST DIRFD NATIVE QEMU PASS] $port actual guest Linux syscalls"
