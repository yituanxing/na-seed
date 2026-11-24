#!/bin/sh
# Shared host dependencies for original signed Alpine ELF QEMU batches.
set -eu
[ "$#" -ge 1 ] || exit 2
port=$1
shift
arm64_efi_fallback=false
if [ "${1-}" = --arm64-efi-fallback ]; then
  arm64_efi_fallback=true
  shift
fi
cross=none
case "${1-}" in
  --cross-c) cross=c; shift ;;
  --cross-cxx) cross=cxx; shift ;;
esac
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/ci-ubuntu-mirror.sh
missing=''
for spec in 'clang:clang' 'ld.lld:lld' 'readelf:binutils' \
            'cpio:cpio' 'xorriso:xorriso' 'mformat:mtools' \
            'sgdisk:gdisk' 'qemu-riscv64:qemu-user' "$@"; do
  bin=${spec%%:*}; pkg=${spec#*:}
  command -v "$bin" >/dev/null || missing="$missing $pkg"
done
case "$port" in
  amd64)
    command -v qemu-system-x86_64 >/dev/null || missing="$missing qemu-system-x86"
    test -f /usr/share/OVMF/OVMF_CODE_4M.fd || \
      test -f /usr/share/OVMF/OVMF_CODE.fd || missing="$missing ovmf" ;;
  riscv64)
    if [ "$cross" != none ]; then
      command -v riscv64-linux-gnu-gcc >/dev/null || missing="$missing gcc-riscv64-linux-gnu"
      if [ "$cross" = cxx ]; then
        command -v riscv64-linux-gnu-g++ >/dev/null || missing="$missing g++-riscv64-linux-gnu"
      fi
      test -f /usr/riscv64-linux-gnu/include/bits/wordsize.h && \
        test -f /usr/riscv64-linux-gnu/lib/libc.a || missing="$missing libc6-dev-riscv64-cross"
    fi
    command -v qemu-system-riscv64 >/dev/null || missing="$missing qemu-system-misc"
    test -f /usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd || missing="$missing qemu-efi-riscv64"
    test -f /usr/lib/ipxe/qemu/efi-virtio.rom || missing="$missing ipxe-qemu" ;;
  arm64)
    if [ "$cross" != none ]; then
      command -v aarch64-linux-gnu-gcc >/dev/null || missing="$missing gcc-aarch64-linux-gnu"
      if [ "$cross" = cxx ]; then
        command -v aarch64-linux-gnu-g++ >/dev/null || missing="$missing g++-aarch64-linux-gnu"
      fi
      test -f /usr/aarch64-linux-gnu/include/bits/wordsize.h && \
        test -f /usr/aarch64-linux-gnu/lib/libc.a || missing="$missing libc6-dev-arm64-cross"
    fi
    command -v qemu-system-aarch64 >/dev/null || missing="$missing qemu-system-arm"
    if [ ! -f /usr/share/AAVMF/AAVMF_CODE.fd ]; then
      if ! "$arm64_efi_fallback" || [ ! -f /usr/share/qemu-efi-aarch64/QEMU_EFI.fd ]; then
        missing="$missing qemu-efi-aarch64"
      fi
    fi
    test -f /usr/lib/ipxe/qemu/efi-virtio.rom || missing="$missing ipxe-qemu" ;;
esac
if [ -n "$missing" ]; then
  sudo apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=30 update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=1 \
    -o Acquire::http::Timeout=30 install -y --no-install-recommends $missing
fi
command -v docker
