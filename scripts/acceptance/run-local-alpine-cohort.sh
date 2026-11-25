#!/bin/sh
# Host-only by default. Native QEMU/APK download is an explicit opt-in;
# never pushes, edits remote branches or triggers GitHub Actions.
set -eu
[ "$#" -ge 1 ] || exit 2
profile=$1
shift
case "$profile" in
    m236) count=7; label=M236; count_word=seven ;;
    m237) count=15; label=M237; count_word=fifteen ;;
    m238) count=15; label=M238; count_word=fifteen ;;
    *) exit 2 ;;
esac
mode=${1:---host-only}
case "$mode" in --host-only|--native) ;; *) exit 2 ;; esac
export PYTHONDONTWRITEBYTECODE=1
python3 scripts/check-m141-candidates.py --profile ${profile}
python3 scripts/test-${profile}-alpine-host.py
python3 scripts/test-m235-alpine-host.py
python3 scripts/test-m223-http-host.py
python3 scripts/test-m205-alpine-host.py
python3 scripts/test-m206-alpine-host.py
python3 scripts/check-three-isa-core.py
python3 scripts/check-architecture-boundaries.py
for path in scripts/probe-m197-alpine-software-cohort.sh \
            scripts/inventory-m141-signed-apks.sh \
            scripts/probe-alpine-profile.sh \
            tests/m205-software-workload.sh \
            tests/${profile}-software-workload.sh; do
    sh -n "$path"
done
python3 -c 'from pathlib import Path
import sys
for suffix in ("alpine-disk-check.py", "alpine-uart.py"):
    path = Path("scripts") / (sys.argv[1] + "-" + suffix)
    compile(path.read_bytes(), str(path), "exec")
path = Path("scripts/alpine-compat-report.py")
compile(path.read_bytes(), str(path), "exec")' "$profile"
if [ "$mode" = --host-only ]; then
    echo "[${label} HOST ONLY PASS] strict manifest, original APK SHA and ledger negative cases; no native-QEMU acceptance claimed"
    exit 0
fi
test "$#" -eq 2 || { echo "usage: sh scripts/acceptance/run-local-alpine-cohort.sh ${profile} --native amd64|arm64|riscv64" >&2; exit 2; }
port=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
# Match M235's successful Ubuntu 24.04 signed-APK and native-QEMU toolchain
# preflight. Host-only tests above never install packages or start QEMU.
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
command -v docker >/dev/null || {
    echo "[${label} NATIVE PREREQUISITE FAIL] Docker required for signed APK inventory" >&2
    exit 2
}
sh scripts/inventory-m141-signed-apks.sh "$port" ${profile}
tsv="build/external/${profile}-candidate-inventory/$port/candidates.tsv"
awk -F '\t' -v expected="$count" 'NR > 1 { n++; if ($3 != "NATIVE_ELF") bad++ }
    END { if (n != expected || bad) exit 1 }' "$tsv"
unique=$(awk -F '\t' 'NR > 1 { print $4 }' "$tsv" | sort -u | wc -l)
test "$unique" -eq "$count" || {
    echo "[${label} FAIL] signed APK original native ELFs share identical SHA; refuse inflated ${count_word}" >&2
    exit 1
}
sh scripts/probe-alpine-profile.sh "$profile" "$port"
