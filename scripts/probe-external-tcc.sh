#!/bin/sh
# Focused host Linux oracle and genuine x86 kernel-QEMU TinyCC acceptance.
set -eu
sh scripts/build-external-tcc.sh amd64
sh scripts/stage-external-tcc-rootfs.sh amd64
export BUSYBOX_PARITY_BINARY=build/external/tcc/amd64/tcc
export NAS_ACCEPTANCE_CPIO=build/external/tcc/amd64/rootfs.cpio
export BUSYBOX_PARITY_MANIFEST=scripts/tcc-native-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
sh scripts/probe-x86-busybox-parity.sh
echo '[TCC NASEED PASS] original TinyCC preprocessor and ELF object compilation under real x86 QEMU'
