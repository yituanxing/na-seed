#!/bin/sh
# Sourced by historical original-program probes after setting port.
# One owner for the real system-QEMU runner and its raw guest serial directory.
# Do not move workload assertions or synthesize success into this mapping.
case "${port:-}" in
  amd64)
    runner=scripts/probe-x86-busybox-parity.sh
    logs=build/busybox-source-parity-x86 ;;
  riscv64)
    runner=scripts/probe-riscv64-busybox-matrix.sh
    logs=build/port-riscv64/full/matrix-source ;;
  arm64)
    runner=scripts/probe-aarch64-busybox-matrix.sh
    logs=build/port-aarch64/full/matrix-source ;;
  *)
    echo "unsupported native QEMU target: ${port:-<unset>}" >&2
    exit 2 ;;
esac
