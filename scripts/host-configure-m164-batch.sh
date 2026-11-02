#!/bin/sh
# Build only the transparent test application with a native Linux GCC ABI.
# The image converters and capture program remain genuine signed Alpine ELFs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m164-batch/amd64/rootfs)
        compiler=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64:build/external/m164-batch/riscv64/rootfs)
        compiler=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64:build/external/m164-batch/arm64/rootfs)
        compiler=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
command -v "$compiler" >/dev/null
mkdir -p "$root/tests"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m164-input-display.c -o "$root/tests/m164-input-display"
readelf -h "$root/tests/m164-input-display" | grep -Fq "$machine"
if readelf -l "$root/tests/m164-input-display" | grep -Fq INTERP; then
    echo '[M164 ELF FAIL] test app unexpectedly needs a loader' >&2
    exit 1
fi
sha256sum "$root/tests/m164-input-display" > "$(dirname "$root")/m164-test-application.sha256"
echo "[M164 LINUX ABI PASS] $port native statically linked three-ISA test app"
