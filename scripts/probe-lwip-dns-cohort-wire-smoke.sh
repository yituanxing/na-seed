#!/bin/sh
# Real x86 VirtIO-Net + official lwIP with three untouched Alpine BIND9
# utilities in one native QEMU boot. A separate host-only UDP parser validates
# every DNS question and returns the fixed RFC 5737 documentation address.
set -eu
[ "$#" -eq 0 ] || {
    echo 'usage: sh scripts/probe-lwip-dns-cohort-wire-smoke.sh' >&2; exit 2;
}
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
# Trace during initial validation; remove after real-wire proof.
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-dns-cohort-wire-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/bin/busybox"
for program in host nslookup mdig; do
    test -s "$root/rootfs/usr/bin/$program"
done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-dns-cohort-wire.pcap"

python3 -u scripts/lwip-dns-wire-server.py > "$root/host-dns-cohort.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT
host_ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[LWIP DNS HOST READY]' "$root/host-dns-cohort.log"; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || {
    echo '[LWIP DNS COHORT FAIL] host DNS responder unavailable' >&2; exit 1;
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP DNS COHORT FAIL] no real PCI VirtIO-Net NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP DNS COHORT FAIL] no DHCP lease' >&2; exit 1;
}
for program in HOST NSLOOKUP MDIG; do
    grep -Fxq "M141_SYSTEM_${program}_DNS_WIRE_PASS" "$clean" || {
        echo "[LWIP DNS COHORT FAIL] original Alpine $program received no valid A reply" >&2;
        exit 1;
    }
done
grep -Fxq M141_SYSTEM_DNS_COHORT_WIRE_PASS "$clean" || {
    echo '[LWIP DNS COHORT FAIL] missing whole-cohort marker' >&2; exit 1;
}
# Three independent guest executables succeeded. Their responses must all
# originate from real questions accepted by the host packet parser, not from
# an in-guest fake DNS resolver or prewritten canned stdout.
host_questions=$(grep -Fc '[LWIP DNS HOST QUERY]' "$root/host-dns-cohort.log" || :)
[ "$host_questions" -ge 3 ] || {
    echo '[LWIP DNS COHORT FAIL] host validated fewer than three DNS packets' >&2;
    exit 1;
}
grep -Fq '[LWIP DNS HOST REPLY] 192.0.2.42' "$root/host-dns-cohort.log" || {
    echo '[LWIP DNS COHORT FAIL] host did not send real DNS answers' >&2; exit 1;
}
echo '[LWIP VIRTIO DNS COHORT QEMU PASS] 3 signed original Alpine BIND9 ELFs: host/nslookup/mdig; one x86 PCI NIC/DHCP/UDP boot'
