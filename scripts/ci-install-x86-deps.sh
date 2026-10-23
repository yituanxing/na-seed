#!/bin/sh
# One bounded, signed Ubuntu apt installation for x86 filesystem/process
# QEMU shards. Native kernel builds and test workloads stay unchanged.
set -eu
missing=''
for spec in 'clang:clang' 'ld.lld:lld' 'xorriso:xorriso' \
            'musl-gcc:musl-tools' 'readelf:binutils' 'cpio:cpio' \
            'qemu-system-x86_64:qemu-system-x86'; do
    bin=${spec%%:*}
    pkg=${spec#*:}
    command -v "$bin" >/dev/null 2>&1 || missing="$missing $pkg"
done
if [ ! -f /usr/share/OVMF/OVMF_CODE_4M.fd ] &&
   [ ! -f /usr/share/OVMF/OVMF_CODE.fd ]; then
    missing="$missing ovmf"
fi
if [ -n "$missing" ]; then
    for file in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/ubuntu.sources; do
        if [ -f "$file" ]; then
            sudo sed -i 's|http://azure.archive.ubuntu.com/ubuntu|http://archive.ubuntu.com/ubuntu|g' "$file"
        fi
    done
    sudo apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=30 update -qq
    sudo DEBIAN_FRONTEND=noninteractive apt-get \
       -o Acquire::Retries=1 -o Acquire::http::Timeout=30 \
       install -y --no-install-recommends $missing
fi
echo '[CI] original x86 FS/process acceptance tools verified'
