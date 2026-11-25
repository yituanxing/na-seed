#!/bin/sh
# x86 filesystem/process and H0–H15 host dependency preparation.
set -eu
[ "$#" -le 1 ] || exit 2
profile=${1:-fs-process}
case "$profile" in
    fs-process) set -- clang:clang ld.lld:lld xorriso:xorriso musl-gcc:musl-tools \
        readelf:binutils cpio:cpio qemu-system-x86_64:qemu-system-x86 ;;
    iso) set -- xorriso:xorriso qemu-system-x86_64:qemu-system-x86 ;;
    base) set -- clang:clang ld.lld:lld cc:gcc xorriso:xorriso qemu-system-x86_64:qemu-system-x86 ;;
    debian) set -- clang:clang ld.lld:lld cc:gcc cmake:cmake dpkg-deb:dpkg \
        readelf:binutils cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs sha256sum:coreutils \
        curl:curl xorriso:xorriso qemu-system-x86_64:qemu-system-x86 ;;
    alpine) set -- clang:clang ld.lld:lld cc:gcc cmake:cmake readelf:binutils \
        cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs sha256sum:coreutils \
        curl:curl tar:tar gzip:gzip openssl:openssl xorriso:xorriso qemu-system-x86_64:qemu-system-x86 ;;
    alpine-python) set -- clang:clang ld.lld:lld cc:gcc cmake:cmake readelf:binutils \
        cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs sha256sum:coreutils \
        curl:curl tar:tar gzip:gzip openssl:openssl python3:python3 xorriso:xorriso qemu-system-x86_64:qemu-system-x86 ;;
    alpine-python-last) set -- clang:clang ld.lld:lld cc:gcc cmake:cmake readelf:binutils \
        cpio:cpio mke2fs:e2fsprogs debugfs:e2fsprogs sha256sum:coreutils \
        curl:curl tar:tar gzip:gzip openssl:openssl xorriso:xorriso python3:python3 qemu-system-x86_64:qemu-system-x86 ;;
    *) exit 2 ;;
esac
missing=''
for spec do
    bin=${spec%%:*}; pkg=${spec#*:}
    command -v "$bin" >/dev/null || missing="$missing $pkg"
done
if [ "$profile" = fs-process ]; then
    if [ ! -f /usr/share/OVMF/OVMF_CODE_4M.fd ] &&
       [ ! -f /usr/share/OVMF/OVMF_CODE.fd ]; then
        missing="$missing ovmf"
    fi
elif [ ! -s /usr/share/OVMF/OVMF_CODE_4M.fd ] &&
   [ ! -s /usr/share/OVMF/OVMF_CODE.fd ]; then
    missing="$missing ovmf"
fi
if [ -n "$missing" ]; then
    if [ "$profile" = fs-process ]; then
        sh scripts/ci-ubuntu-mirror.sh
        sudo apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=30 update -qq
        sudo DEBIAN_FRONTEND=noninteractive apt-get \
            -o Acquire::Retries=1 -o Acquire::http::Timeout=30 \
            install -y --no-install-recommends $missing
    else
        sudo apt-get update -qq
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $missing
    fi
fi
if [ "$profile" = fs-process ]; then
    echo '[CI] original x86 FS/process acceptance tools verified'
fi
