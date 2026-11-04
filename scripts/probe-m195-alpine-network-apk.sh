#!/bin/sh
# Real Alpine rootfs networking: official signed APK installed across a native
# VirtIO-Net/slirp wire, then same physical ext4 disk reopened by a new QEMU.
# Never weaken M191-M194 nor confuse local deterministic wire with Internet.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m195-alpine-network-apk.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

# This is a genuinely network-capable root kernel on ALL three ISAs. The
# identical physical-root M191 oracle remains mandatory and independent.
export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

repo="$PWD/$work/m195-original-repository"
python3 scripts/m195-alpine-repository.py "$port" "$repo"
test -s "$repo/repositories" && test -s "$repo/proof.txt"

# The signed baseline never changes. These are ordinary host-side test
# configuration changes (serial credential and a scoped repository), NOT
# forged Alpine binaries or a replaced init/service implementation.
overlay=$(mktemp -d "$PWD/$work/m195-network-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
sudo install -m 0644 "$repo/repositories" "$overlay/root/m195-repositories"
test ! -e "$overlay/usr/bin/tree"
test ! -e "$overlay/root/m195-wire.txt"
disk="$PWD/$work/m195-network-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

server_log="$PWD/$work/m195-host-http.log"
pcap="$PWD/$work/m195-wire.pcap"
test ! -e "$pcap" || { echo '[M195 WIRE FAIL] stale packet capture'; exit 1; }
python3 -u scripts/m195-alpine-http.py "$repo" 15417 > "$server_log" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || :; wait "$server_pid" 2>/dev/null || :' EXIT HUP INT TERM
ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --noproxy '*' -fsS --max-time 2 http://127.0.0.1:15417/healthz -o /dev/null; then
        ready=1; break
    fi
    sleep 1
done
[ "$ready" -eq 1 ] || { echo '[M195 WIRE FAIL] host package server unavailable' >&2; exit 1; }

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
export NAS_VIRTIO_NET_PCAP="$pcap"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m195-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m195-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M195_DIAG_SECONDS:-210}
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/m191-root-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

fifo="$PWD/$work/m195-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
live_serial="$PWD/$logs/m195-batch.serial.log"
python3 scripts/m195-alpine-uart.py "$runner" "$live_serial" \
    "$PWD/$work/m195-first-runner.log" "$fifo"
test -s "$live_serial" && test -s "$pcap" && test -s "$server_log"
cp "$live_serial" "$PWD/$work/m195-live.serial.log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$live_serial" || {
    echo '[M195 WIRE FAIL] genuine VirtIO-Net DHCP lease not observed' >&2; exit 1;
}
for path in /proof.txt /"$(cat "$repo/package-path")"; do
    grep -Fq "GET path=$path " "$server_log" || {
        echo "[M195 WIRE FAIL] host did not observe $path" >&2; exit 1;
    }
    grep -aFq "GET $path " "$pcap" || {
        echo "[M195 WIRE FAIL] QEMU PCAP did not capture $path" >&2; exit 1;
    }
done
grep -Fq "GET path=/$(cat "$repo/index-path") " "$server_log" || {
    echo '[M195 WIRE FAIL] guest did not request official APKINDEX' >&2; exit 1;
}
grep -aFq 'M195_SIGNED_ALPINE_WIRE_2026' "$pcap" || {
    echo '[M195 WIRE FAIL] actual HTTP response absent from QEMU capture' >&2; exit 1;
}
echo "[M195 ALPINE LIVE WIRE PASS] $port real DHCP, signed apk index+package "\
     'GET, QEMU PCAP request/response, native tree ELF, guest ext4 sync'

# First QEMU was deliberately stopped only AFTER guest sync; a distinct
# physical QEMU now boots this very disk, runs real newly installed tree,
# exits zero, and lets lwext4 unmount/flush cleanly for host e2fsck.
unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP
export BUSYBOX_PARITY_MANIFEST=scripts/m195-alpine-reopen-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M195_REOPEN_SECONDS:-170}
if ! sh "$runner" > "$PWD/$work/m195-reopen-runner.log" 2>&1; then
    echo "[M195 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m195-reopen-runner.log" >&2
    exit 1
fi
reopen="$PWD/$logs/m195-batch.clean.log"
for marker in \
    'M195_SIGNED_ALPINE_WIRE_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$marker" "$reopen" || {
        echo "[M195 ALPINE REOPEN FAIL] $port missing $marker" >&2
        tail -120 "$reopen" >&2
        exit 1
    }
done
sudo python3 scripts/m195-alpine-disk-check.py "$port" "$root" "$disk"
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M195 ALPINE ONLINE APK PERSISTENCE PASS] $port signed untouched Alpine "\
     'APK installed over real network into ext4, original ELF ran, second ' \
     'QEMU boot+host disk readback/fsck; local HTTP wire, NOT public Internet'
