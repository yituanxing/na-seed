#!/bin/sh
# Compile the three separate NaSeed userland desktop programs for the guest ISA.
# Each client is a true exec'ed ELF, not an in-process mode of the compositor.
# Original Alpine program executables remain byte-identical signed APK ELFs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port:$root" in
    amd64:build/external/m167-batch/amd64/rootfs)
        compiler=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64:build/external/m167-batch/riscv64/rootfs)
        compiler=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64:build/external/m167-batch/arm64/rootfs)
        compiler=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
command -v "$compiler" >/dev/null
mkdir -p "$root/usr/bin"
for app in naseed-desktop naseed-image naseed-terminal; do
    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$root/usr/bin/$app"
    [ "$(od -An -tx1 -N4 "$root/usr/bin/$app" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$root/usr/bin/$app" | grep -Fq "$machine" || exit 1
    if readelf -l "$root/usr/bin/$app" | grep -Fq INTERP; then
        echo "[M167 APP FAIL] $app ELF unexpectedly dynamic" >&2
        exit 1
    fi
done
sha256sum "$root/usr/bin/naseed-desktop" "$root/usr/bin/naseed-image" \
    "$root/usr/bin/naseed-terminal" > "$(dirname "$root")/m167-native-applications.sha256"
echo "[M167 LINUX ABI PASS] $port three fully-static separately executable native desktop programs"
