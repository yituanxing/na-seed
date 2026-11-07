#!/bin/sh
# Modern PCI VirtIO-Net + official lwIP + unchanged signed Alpine Ncat UDP
# to a deterministic host-only echo server over QEMU slirp (not loopback).
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-udp-wire-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-udp-wire-cases.tsv

# Run only untouched, signed Alpine binaries from the upstream APK payload.
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/ncat"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-udp-wire.pcap"

python3 -u scripts/lwip-udp-echo-server.py > "$root/host-udp.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT
host_ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[LWIP UDP HOST READY]' "$root/host-udp.log"; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || { echo '[LWIP UDP WIRE FAIL] host UDP server unavailable' >&2; exit 1; }

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP UDP WIRE FAIL] no PCI NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP UDP WIRE FAIL] no DHCP lease' >&2; exit 1;
}
grep -Fxq M141_SYSTEM_NCAT_UDP_WIRE_PASS "$clean" || {
    echo '[LWIP UDP WIRE FAIL] signed guest Ncat received no echo' >&2; exit 1;
}
grep -Fq '[LWIP UDP HOST REQUEST] exact signed-Ncat payload' "$root/host-udp.log" || {
    echo '[LWIP UDP WIRE FAIL] host received no authentic guest datagram' >&2; exit 1;
}
echo '[LWIP VIRTIO UDP WIRE QEMU PASS] real x86 PCI NIC, DHCP, signed Alpine Ncat UDP guest-host echo'
