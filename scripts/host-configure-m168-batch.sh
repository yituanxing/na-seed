#!/bin/sh
# Build four independent production Linux ABI desktop ELFs on the host.
# All signed Alpine program binaries remain byte-identical to their APKs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m168-batch/amd64/rootfs)
        compiler=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64:build/external/m168-batch/riscv64/rootfs)
        compiler=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64:build/external/m168-batch/arm64/rootfs)
        compiler=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
command -v "$compiler" >/dev/null
mkdir -p "$root/usr/bin"
for app in naseed-desktop naseed-image naseed-terminal naseed-browser; do
    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$root/usr/bin/$app"
    [ "$(od -An -tx1 -N4 "$root/usr/bin/$app" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$root/usr/bin/$app" | grep -Fq "$machine"
    if readelf -l "$root/usr/bin/$app" | grep -Fq INTERP; then
        echo "[M168 APP FAIL] dynamic native client $app" >&2
        exit 1
    fi
done
sha256sum "$root/usr/bin/naseed-desktop" "$root/usr/bin/naseed-image" \
    "$root/usr/bin/naseed-terminal" "$root/usr/bin/naseed-browser" \
    > "$(dirname "$root")/m168-native-applications.sha256"
echo "[M168 ABI PASS] $port four independently executable static desktop ELFs"
