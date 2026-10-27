#!/bin/sh
set -eu
for target in x86_64 riscv64 aarch64; do
    sh scripts/build-lwext4-guest.sh "$target" "build/lwext4-guest/$target" closed
done
