#!/bin/sh
# Two real modern VirtIO-Net PCI devices -> U-Boot NICs -> independent lwIP
# DHCP, plus untouched Alpine Ncat TCP to host over the first QEMU slirp.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-virtio-wire-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_VIRTIO_NET_SECOND_USER=1
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
grep -Fq '[VIRTIO-NET] real modern PCI devices=2' "$clean" || {
    echo '[LWIP WIRE FAIL] two real PCI NICs not bound' >&2; exit 1;
}
for name in eth0 eth1; do
    grep -Fq "[LWIP] native $name attached; DHCP started" "$clean" || {
        echo "[LWIP WIRE FAIL] $name netif/DHCP not started" >&2; exit 1;
    }
done
for prefix in 10.0.2. 10.0.3.; do
    grep -Fq "[LWIP DHCP BOUND] addr=$prefix" "$clean" || {
        echo "[LWIP WIRE FAIL] no independently verified $prefix DHCP lease" >&2; exit 1;
    }
done
echo '[LWIP VIRTIO WIRE QEMU PASS] x86 two PCI NICs, two DHCP leases, signed Alpine Ncat TCP to host'
