#!/bin/sh
# Official Alpine /etc/apk/repositories -> PUBLIC DNS -> PUBLIC HTTPS ->
# signed tree+jq packages -> native ELF+shared libs -> durable ext4 reboot.
# This is deliberately a separate canary from deterministic local M195.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m196-alpine-public-apk.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) alpine_arch=x86_64 ;;
    riscv64) alpine_arch=riscv64 ;;
    arm64) alpine_arch=aarch64 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh

export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
# The first real public-CDN canary exposed a shared musl DNS receive error
# after genuine UDP replies arrived on the wire. Trace the exact x86 socket
# ABI results while keeping RV64/AArch64 production-style execution quiet.
if [ "$port" = amd64 ]; then
    export NAS_SYSCALL_TRACE=1
else
    unset NAS_SYSCALL_TRACE
fi
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

# GitHub-hosted runner must first show its own official CDN route works.
# A broken public runner/CDN is explicitly INFRA FAIL, never a guest PASS.
public_index="https://dl-cdn.alpinelinux.org/alpine/v3.23/main/$alpine_arch/APKINDEX.tar.gz"
if ! curl --noproxy '*' -fIsS --retry 2 --retry-delay 2 \
          --connect-timeout 12 --max-time 40 "$public_index" >/dev/null; then
    echo '[M196 PUBLIC INFRA FAIL] runner cannot reach official Alpine HTTPS CDN' >&2
    exit 1
fi

# Only mutable disposable configuration is DNS for the documented QEMU
# user-mode network. Guest remains original signed Alpine init, APK, CA,
# package signing keys and official HTTPS repositories, NOT a host proxy.
overlay=$(mktemp -d "$PWD/$work/m196-public-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
sudo python3 scripts/m196-alpine-stage.py "$root" "$overlay"
sudo test ! -e "$overlay/root/m196-wan.txt" || {
    echo '[M196 PUBLIC STAGE FAIL] public witness was preseeded' >&2
    exit 1
}
disk="$PWD/$work/m196-public-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
pcap="$PWD/$work/m196-public-wire.pcap"
wire_json="$PWD/$work/m196-public-wire.json"
test ! -e "$pcap" || { echo '[M196 PUBLIC WIRE FAIL] stale PCAP'; exit 1; }
export NAS_VIRTIO_NET_PCAP="$pcap"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m196-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m196-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M196_PUBLIC_QEMU_TIMEOUT:-600}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

fifo="$PWD/$work/m196-public-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m196-batch.serial.log"
python3 scripts/m196-alpine-uart.py "$runner" "$serial" \
    "$PWD/$work/m196-first-runner.log" "$fifo"
test -s "$serial" && test -s "$pcap"
cp "$serial" "$PWD/$work/m196-live.serial.log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$serial" || {
    echo '[M196 PUBLIC WIRE FAIL] no real VirtIO DHCP lease' >&2
    exit 1
}
python3 scripts/m196-public-wire-check.py "$pcap" "$wire_json"
echo "[M196 ALPINE PUBLIC LIVE PASS] $port original public HTTPS signed APK "\
     'DNS+TLS from real VirtIO NIC, tree+jq dynamic ELF and guest ext4 sync'

# A NEW QEMU instance reopens the SAME physical disk, with no network
# needed for the installed apps: this proves they were not staged or cached.
unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP
export BUSYBOX_PARITY_MANIFEST=scripts/m196-alpine-reopen-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M196_REOPEN_SECONDS:-180}
if ! sh "$runner" > "$PWD/$work/m196-reopen-runner.log" 2>&1; then
    echo "[M196 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m196-reopen-runner.log" >&2
    exit 1
fi
reopen="$PWD/$logs/m196-batch.clean.log"
for marker in \
    'M196_PUBLIC_ALPINE_APK_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$marker" "$reopen" || {
        echo "[M196 ALPINE REOPEN FAIL] $port missing $marker" >&2
        tail -120 "$reopen" >&2
        exit 1
    }
done
sudo python3 scripts/m196-alpine-disk-check.py "$port" "$root" "$disk"
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M196 ALPINE PUBLIC APK PERSISTENCE PASS] $port genuine public DNS+HTTPS "\
     'official signed Alpine tree+jq, new native ELF/shared libraries, ' \
     'second native QEMU boot, original baseline intact and ext4 fsck'
