#!/bin/sh
# Actual modern VirtIO-Net PCI -> U-Boot NIC -> lwIP DHCP -> Alpine Ncat TCP
# to a host HTTP service over QEMU slirp. Separate from pure lo acceptance.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-virtio-wire-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
# Temporary focused ABI diagnosis: records actual native Ncat syscall
# arguments/status while keeping the signed executable untouched.
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-wire-cases.tsv

# The guest runs an untouched Alpine binary; the host stages signed APKs.
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/ncat"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-wire.pcap"

# QEMU slirp exposes host loopback services through gateway 10.0.2.2.
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
[ "$host_ready" = true ] || { echo '[LWIP WIRE FAIL] host HTTP server unavailable' >&2; exit 1; }

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP WIRE FAIL] real PCI NIC not bound' >&2; exit 1;
}
grep -Fq '[LWIP] native eth0 attached; DHCP started' "$clean" || {
    echo '[LWIP WIRE FAIL] NIC netif/DHCP not started' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP WIRE FAIL] no verified QEMU DHCP lease' >&2; exit 1;
}
echo '[LWIP VIRTIO WIRE QEMU PASS] x86 modern PCI, DHCP lease, signed Alpine Ncat TCP to host'
