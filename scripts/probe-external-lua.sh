#!/bin/sh
# Fast targeted non-BusyBox acceptance, one kernel per ISA, two real programs.
# This reuses existing QEMU/Limine launchers without running BusyBox itself.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/build-external-lua.sh "$port"
sh scripts/stage-external-lua-rootfs.sh "$port"
work="build/external/lua/$port"
export BUSYBOX_PARITY_BINARY="$work/lua"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/lua-native-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
# Underlying launcher retains strict native stdout + exit status checks.
sh "$runner"
echo "[LUA NASEED QEMU PASS] $port: 2 original upstream interpreter workloads"
