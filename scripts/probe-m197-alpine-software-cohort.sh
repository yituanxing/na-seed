#!/bin/sh
# Real original Alpine PID1 on physical ext4: direct public HTTPS one-transaction
# install of Bash/Lua/SQLite/Git, integrated work and independent second QEMU.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m197-alpine-software-cohort.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) alpine_arch=x86_64 ;;
    riscv64) alpine_arch=riscv64 ;;
    arm64) alpine_arch=aarch64 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh

export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
unset NAS_SYSCALL_TRACE
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

# An external mirror failure is a clear CI infrastructure failure, never a
# guest success or a reason to silently substitute the local M195 repository.
public_index="https://dl-cdn.alpinelinux.org/alpine/v3.23/main/$alpine_arch/APKINDEX.tar.gz"
if ! curl --noproxy '*' -fIsS --retry 2 --retry-delay 2 \
          --connect-timeout 12 --max-time 40 "$public_index" >/dev/null; then
    echo '[M197 PUBLIC INFRA FAIL] GitHub runner cannot reach official Alpine HTTPS CDN' >&2
    exit 1
fi

overlay=$(mktemp -d "$PWD/$work/m197-software-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
sudo python3 scripts/m197-alpine-stage.py "$root" "$overlay"
sudo install -m 0644 tests/m197-software-workload.sh \
    "$overlay/root/m197-software-workload.sh"
sudo test -s "$overlay/root/m197-software-workload.sh"
sudo test ! -e "$overlay/root/m197-app" || {
    echo '[M197 PUBLIC STAGE FAIL] application output was preseeded' >&2; exit 1;
}
sudo test ! -e "$overlay/root/m197-wan.txt" || {
    echo '[M197 PUBLIC STAGE FAIL] persistence witness was preseeded' >&2; exit 1;
}
disk="$PWD/$work/m197-software-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
pcap="$PWD/$work/m197-public-wire.pcap"
wire_json="$PWD/$work/m197-public-wire.json"
test ! -e "$pcap" || { echo '[M197 PUBLIC WIRE FAIL] stale PCAP'; exit 1; }
export NAS_VIRTIO_NET_PCAP="$pcap"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m197-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_PUBLIC_QEMU_TIMEOUT:-1020}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

fifo="$PWD/$work/m197-software-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m197-batch.serial.log"
python3 scripts/m197-alpine-uart.py "$runner" "$serial" \
    "$PWD/$work/m197-first-runner.log" "$fifo"
test -s "$serial" && test -s "$pcap"
cp "$serial" "$PWD/$work/m197-live.serial.log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$serial" || {
    echo '[M197 PUBLIC WIRE FAIL] no real VirtIO DHCP lease' >&2
    exit 1
}
# M196's strict independent parser demands actual guest DNS query+answer,
# a public TCP three-way handshake and TLS payload, not just guest markers.
python3 scripts/m196-public-wire-check.py "$pcap" "$wire_json"
grep -Fq 'M197_APP_WORKLOAD_PASS' "$serial" || {
    echo '[M197 PUBLIC WORKLOAD FAIL] no genuine integrated app result' >&2
    exit 1
}
echo "[M197 ALPINE PUBLIC SOFTWARE LIVE PASS] $port official signed "\
     'Bash Lua SQLite Git from public HTTPS, live DHCP resolver, ' \
     'independent PCAP and four real applications on ext4'

# A SECOND native QEMU has NO network requirement; it must observe and run
# the genuine installed ELFs and all SQL/Git/application state from disk.
unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP
export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-reopen-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_REOPEN_SECONDS:-240}
if ! sh "$runner" > "$PWD/$work/m197-reopen-runner.log" 2>&1; then
    echo "[M197 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m197-reopen-runner.log" >&2
    exit 1
fi
reopen="$PWD/$logs/m197-batch.clean.log"
for marker in \
    'M197_REOPEN_WORKLOAD_PASS' \
    'M197_PUBLIC_ALPINE_SOFTWARE_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$marker" "$reopen" || {
        echo "[M197 ALPINE REOPEN FAIL] $port missing $marker" >&2
        tail -120 "$reopen" >&2
        exit 1
    }
done
sudo python3 scripts/m197-alpine-disk-check.py "$port" "$root" "$disk"
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M197 ALPINE PUBLIC SOFTWARE PERSISTENCE PASS] $port original "\
     'Alpine public signed four-package transaction, native Bash/Lua/SQLite/Git, ' \
     'second real QEMU, exact Git+SQL host disk oracle and ext4 fsck'
