#!/bin/sh
# PCI VirtIO-Net + upstream lwIP + untouched signed Alpine BIND dig over UDP.
# The response comes only from the independent host DNS packet responder.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-dns-wire-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
# Keep syscall evidence during this initial independent DNS ABI validation.
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-dns-wire-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/dig"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-dns-wire.pcap"

python3 -u scripts/lwip-dns-wire-server.py > "$root/host-dns.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT
host_ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[LWIP DNS HOST READY]' "$root/host-dns.log"; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || { echo '[LWIP DNS WIRE FAIL] host DNS responder unavailable' >&2; exit 1; }

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP DNS WIRE FAIL] no PCI NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP DNS WIRE FAIL] no DHCP lease' >&2; exit 1;
}
grep -Fxq M141_SYSTEM_DIG_DNS_WIRE_PASS "$clean" || {
    echo '[LWIP DNS WIRE FAIL] signed guest dig did not verify response' >&2; exit 1;
}
grep -Fq '[LWIP DNS HOST QUERY] exact signed-dig A naseed.test.' "$root/host-dns.log" || {
    echo '[LWIP DNS WIRE FAIL] host never parsed an authentic guest DNS request' >&2; exit 1;
}
grep -Fq '[LWIP DNS HOST REPLY] 192.0.2.42' "$root/host-dns.log" || {
    echo '[LWIP DNS WIRE FAIL] host never sent the deterministic DNS answer' >&2; exit 1;
}
echo '[LWIP VIRTIO DNS WIRE QEMU PASS] real x86 PCI NIC, DHCP, signed Alpine dig UDP DNS request/answer'
