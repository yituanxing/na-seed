#!/bin/sh
# Back-compatible RV64 artifact path. The upstream version, package recipe,
# checksum and guest applets are now defined by the SAME shared fixture used
# for x86-64; no architecture-local BusyBox choice.
set -eu
sh scripts/fetch-busybox-fixture.sh riscv64 build/port-riscv64/full/busybox
