#!/bin/sh
# Two real RV64/ARM64 VirtIO-MMIO NICs -> independent official lwIP DHCP
# -> untouched signed Alpine Ncat -> host-only HTTP over the first slirp.
# Single targeted smoke per ISA, not a full M141 software matrix.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-lwip-virtio-wire-multiarch.sh riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_VIRTIO_NET_SECOND_USER=1
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-wire-cases.tsv

# Signed Alpine originals are never recompiled, patched or replaced with
# synthetic guest network clients. Build the kernel explicitly under the
# opt-in flags; the generic matrix runner otherwise reuses an existing ELF.
sh scripts/stage-m141-prebuilt.sh "$port"
root="build/external/m141-batch/$port"
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/ncat"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-wire.pcap"
case "$port" in
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64)   sh scripts/build-aarch64-full.sh ;;
esac

# QEMU slirp maps host loopback to the guest gateway 10.0.2.2. This page is
# absent from guest CPIO; the response therefore proves actual guest RX.
python3 -m http.server 15413 --bind 127.0.0.1 --directory . \
    > "$root/host-http.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT
host_ready=false
for attempt in 1 2 3 4 5; do
    if curl --noproxy '*' -fsS --max-time 2 \
        http://127.0.0.1:15413/README.md -o /dev/null; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || { echo '[LWIP MMIO WIRE FAIL] host HTTP server unavailable' >&2; exit 1; }

sh "$runner"
clean="$logs/m141-batch.clean.log"
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern MMIO devices=2' "$clean" || {
    echo "[LWIP MMIO WIRE FAIL] $port two real MMIO NICs not bound" >&2; exit 1;
}
for name in eth0 eth1; do
    grep -Fq "[LWIP] native $name attached; DHCP started" "$clean" || {
        echo "[LWIP MMIO WIRE FAIL] $port $name netif/DHCP not started" >&2; exit 1;
    }
done
for prefix in 10.0.2. 10.0.3.; do
    grep -Fq "[LWIP DHCP BOUND] addr=$prefix" "$clean" || {
        echo "[LWIP MMIO WIRE FAIL] $port no $prefix DHCP lease" >&2; exit 1;
    }
done
grep -Fxq M141_SYSTEM_NCAT_WIRE_PASS "$clean" || {
    echo "[LWIP MMIO WIRE FAIL] $port no signed Alpine Ncat HTTP reply" >&2; exit 1;
}
echo "[LWIP VIRTIO MMIO WIRE QEMU PASS] $port two real NICs, two DHCP leases, signed Alpine Ncat TCP to host"
