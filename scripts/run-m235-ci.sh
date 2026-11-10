#!/bin/sh
# One CI owner reused by AMD64 pilot and the gated RV64/ARM64 pair.
# No third QEMU, no per-program harness, no alternate Alpine rootfs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
python3 scripts/check-m141-candidates.py --profile m235
python3 scripts/test-m235-alpine-host.py
python3 scripts/test-m223-http-host.py
python3 scripts/test-m205-alpine-host.py
python3 scripts/test-m206-alpine-host.py
for script in scripts/probe-m197-alpine-software-cohort.sh \
              scripts/probe-m235-alpine-batch.sh \
              scripts/inventory-m141-signed-apks.sh \
              tests/m205-software-workload.sh tests/m235-software-workload.sh; do
    sh -n "$script"
done
python3 -m py_compile scripts/m235-alpine-uart.py scripts/m235-compat-report.py \
    scripts/m235-alpine-disk-check.py scripts/m235-http-proof.py

sh scripts/ci-ubuntu-mirror.sh
missing=
for spec in 'cmake:cmake' 'g++:g++' 'clang:clang' 'ld.lld:lld' \
            'readelf:binutils' 'cpio:cpio' 'mke2fs:e2fsprogs' \
            'debugfs:e2fsprogs' 'e2fsck:e2fsprogs' 'xorriso:xorriso' \
            'mformat:mtools' 'sgdisk:gdisk' 'curl:curl' \
            'python3:python3' 'openssl:openssl'; do
    bin=${spec%%:*}; pkg=${spec#*:}
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
command -v docker >/dev/null
sh scripts/inventory-m141-signed-apks.sh "$port" m235
tsv="build/external/m235-candidate-inventory/$port/candidates.tsv"
awk -F '\t' 'NR > 1 { n++; if ($3 != "NATIVE_ELF") bad++ } END {
    if (n != 12 || bad) { print "[M235 FAIL] wrong signed native ELF census"; exit 1 }
}' "$tsv"
unique=$(awk -F '\t' 'NR > 1 { print $4 }' "$tsv" | sort -u | wc -l)
test "$unique" -eq 12 || { echo '[M235 FAIL] duplicate signed ELF SHA' >&2; exit 1; }
echo "[M235 SIGNED APK PASS] $port twelve newly signed target-native unique ELF hashes"
sh scripts/probe-m235-alpine-batch.sh "$port"
