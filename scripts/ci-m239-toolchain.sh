#!/bin/sh
# GitHub-hosted Ubuntu 24.04 only. Build dependencies are installed on HOST;
# the NaSeed guest runs untouched official signed Alpine native ELF packages.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
PATH=/usr/sbin:/sbin:$PATH
export PATH
sh scripts/ci-ubuntu-mirror.sh
missing=
for spec in 'cmake:cmake' 'g++:g++' 'clang:clang' 'ld.lld:lld' \
            'readelf:binutils' 'cpio:cpio' 'mke2fs:e2fsprogs' \
            'debugfs:e2fsprogs' 'e2fsck:e2fsprogs' 'xorriso:xorriso' \
            'mformat:mtools' 'sgdisk:gdisk' 'curl:curl' \
            'python3:python3' 'openssl:openssl'; do
    bin=${spec%%:*}
    pkg=${spec#*:}
    command -v "$bin" >/dev/null || missing="$missing $pkg"
done
case "$port" in
    amd64)
        command -v qemu-system-x86_64 >/dev/null || missing="$missing qemu-system-x86"
        test -f /usr/share/OVMF/OVMF_CODE_4M.fd || \
            test -f /usr/share/OVMF/OVMF_CODE.fd || missing="$missing ovmf" ;;
    riscv64)
        command -v riscv64-linux-gnu-gcc >/dev/null || missing="$missing gcc-riscv64-linux-gnu"
        command -v riscv64-linux-gnu-g++ >/dev/null || missing="$missing g++-riscv64-linux-gnu"
        test -f /usr/riscv64-linux-gnu/lib/libc.a || missing="$missing libc6-dev-riscv64-cross"
        command -v qemu-system-riscv64 >/dev/null || missing="$missing qemu-system-misc"
        test -f /usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd || missing="$missing qemu-efi-riscv64"
        test -f /usr/lib/ipxe/qemu/efi-virtio.rom || missing="$missing ipxe-qemu" ;;
    arm64)
        command -v aarch64-linux-gnu-gcc >/dev/null || missing="$missing gcc-aarch64-linux-gnu"
        command -v aarch64-linux-gnu-g++ >/dev/null || missing="$missing g++-aarch64-linux-gnu"
        test -f /usr/aarch64-linux-gnu/lib/libc.a || missing="$missing libc6-dev-arm64-cross"
        command -v qemu-system-aarch64 >/dev/null || missing="$missing qemu-system-arm"
        test -f /usr/share/AAVMF/AAVMF_CODE.fd || \
            test -f /usr/share/qemu-efi-aarch64/QEMU_EFI.fd || missing="$missing qemu-efi-aarch64"
        test -f /usr/lib/ipxe/qemu/efi-virtio.rom || missing="$missing ipxe-qemu" ;;
esac
if [ -n "$missing" ]; then
    sudo apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=30 update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=1 \
        -o Acquire::http::Timeout=30 install -y --no-install-recommends $missing
fi
command -v docker >/dev/null || {
    echo '[M239 INFRA FAIL] Docker needed for authentic Alpine signed APK media' >&2
    exit 2
}
