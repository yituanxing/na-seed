#!/bin/sh
# Two untouched signed Alpine networking ELFs, ONE x86 QEMU boot and ONE
# host iperf3 server. Nmap first scans the host port over real TCP; iperf3
# then completes its authentic control/data TCP exchange over VirtIO/lwIP.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-iperf3-wire-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
# Initial proof retains Linux syscall diagnostics for actionable ABI failures.
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-iperf3-wire-cases.tsv

command -v iperf3 >/dev/null || {
    echo '[LWIP IPERF WIRE FAIL] native host iperf3 missing' >&2
    exit 1
}
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/iperf3"
test -s "$root/rootfs/usr/bin/nmap"
test -s "$root/rootfs/bin/busybox"
test -s "$root/rootfs/tests/m141-iperf3-wire-run.sh"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-iperf3-wire.pcap"

# QEMU's gateway 10.0.2.2 exposes loopback-bound services on its host.
# Keep the server alive across Nmap's short connect-and-close scan before
# iperf3 establishes its actual control and data streams. No fake iperf3
# payload, synthetic echo, or custom host protocol is involved.
stdbuf -oL -eL iperf3 -s -B 127.0.0.1 -p 15416 > "$root/host-iperf3.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT
sleep 2
kill -0 "$host_server_pid" 2>/dev/null || {
    echo '[LWIP IPERF WIRE FAIL] host iperf3 server terminated early' >&2
    exit 1
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP IPERF WIRE FAIL] no real PCI NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP IPERF WIRE FAIL] no real DHCP lease' >&2; exit 1;
}
grep -Fxq M141_SIGNED_NMAP_WIRE_PASS "$clean" || {
    echo '[LWIP IPERF WIRE FAIL] signed Nmap scan did not find host port' >&2; exit 1;
}
grep -Fxq M141_SIGNED_IPERF3_WIRE_PASS "$clean" || {
    echo '[LWIP IPERF WIRE FAIL] signed iperf3 did not transfer bytes' >&2; exit 1;
}
grep -Fxq M141_SIGNED_NMAP_IPERF3_WIRE_PASS "$clean" || {
    echo '[LWIP IPERF WIRE FAIL] guest combined case did not exit cleanly' >&2; exit 1;
}
# The host, independently of guest stdout, must report its own true receiver.
grep -Eq '[[:space:]]receiver[[:space:]]*$' "$root/host-iperf3.log" || {
    echo '[LWIP IPERF WIRE FAIL] host reported no real receiving stream' >&2; exit 1;
}
awk '$NF=="receiver" && $5+0>0 {nonzero=1} END {exit nonzero?0:1}' \
    "$root/host-iperf3.log" || {
    echo '[LWIP IPERF WIRE FAIL] host receiver reported zero bytes' >&2; exit 1;
}
if grep -Fq 'client has unexpectedly closed the connection' \
   "$root/host-iperf3.log"; then
    echo '[LWIP IPERF WIRE FAIL] control stream closed before IPERF_DONE' >&2
    exit 1
fi
echo '[LWIP VIRTIO IPERF WIRE QEMU PASS] original signed Alpine Nmap port scan and iperf3 TCP transfer over real PCI/DHCP/lwIP'
