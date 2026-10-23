#!/bin/sh
# Same source, acceptance manifest and full shared Task/VM/FS/ELF kernel,
# only native ABI and existing hardware runner differ by target ISA.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64) runner=scripts/probe-x86-busybox-parity.sh ;;
  riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh ;;
  arm64) runner=scripts/probe-aarch64-busybox-matrix.sh ;;
  *) echo "unsupported ISA: $1" >&2; exit 2 ;;
esac
sh scripts/build-m33-native-tcc.sh "$1"
sh scripts/stage-m33-native-tcc-rootfs.sh "$1"
work="build/external/m33-tcc/$1"
export BUSYBOX_PARITY_BINARY="$work/tcc"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m33-native-tcc-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
sh "$runner"
echo "[M33 NATIVE TCC PASS] $1: real Linux TinyCC ELF preprocesses C and writes ELF object on NaSeed"
