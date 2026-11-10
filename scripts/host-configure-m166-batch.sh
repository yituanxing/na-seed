#!/bin/sh
# Host-only test application: original signed Alpine tools are not rebuilt.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m166-batch/amd64/rootfs)
        compiler=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64:build/external/m166-batch/riscv64/rootfs)
        compiler=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64:build/external/m166-batch/arm64/rootfs)
        compiler=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
command -v "$compiler" >/dev/null
mkdir -p "$root/tests"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m166-input-display.c -o "$root/tests/m166-input-display"
readelf -h "$root/tests/m166-input-display" | grep -Fq "$machine"
if readelf -l "$root/tests/m166-input-display" | grep -Fq INTERP; then
    echo '[M166 APP FAIL] acceptance ELF unexpectedly dynamic' >&2
    exit 1
fi
sha256sum "$root/tests/m166-input-display" > "$(dirname "$root")/m166-test-application.sha256"
echo "[M166 LINUX ABI PASS] $port native static input application"
