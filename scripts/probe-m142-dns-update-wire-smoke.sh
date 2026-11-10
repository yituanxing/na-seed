#!/bin/sh
# Real x86 PCI VirtIO-Net + lwIP, original Alpine nsupdate RFC 2136 update,
# original host query and Nping unprivileged UDP packet. The Python host owns
# all DNS state independently and validates every packet on the wire.
set -eu
[ "$#" -eq 0 ] || {
    echo 'usage: sh scripts/probe-m142-dns-update-wire-smoke.sh' >&2; exit 2;
}
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m142-dns-update-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m141-original-signed-elfs.sha256"
test -s "$root/rootfs/bin/busybox"
test -s "$root/rootfs/tests/m142-dns-update-run.sh" || {
    echo '[M142 DDNS WIRE FAIL] guest script missing from canonical signed CPIO' >&2;
    exit 1;
}
for program in nsupdate host nping; do
    test -s "$root/rootfs/usr/bin/$program"
    grep -Fxq "usr/bin/$program" scripts/m141-original-elfs.txt || {
        echo "[M142 DDNS WIRE FAIL] unsigned ELF $program" >&2; exit 1;
    }
done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-m142-dns-update.pcap"

python3 -u scripts/m142-dns-update-server.py > "$root/host-m142-dns-update.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
host_ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[M142 DNS HOST READY]' "$root/host-m142-dns-update.log"; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || {
    echo '[M142 DDNS WIRE FAIL] host RFC2136 responder unavailable' >&2; exit 1;
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m142-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[M142 DDNS WIRE FAIL] missing actual PCI NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[M142 DDNS WIRE FAIL] missing real DHCP lease' >&2; exit 1;
}
for marker in M142_ORIGINAL_NSUPDATE_RFC2136_COMMAND_PASS \
              M142_ORIGINAL_HOST_POST_UPDATE_QUERY_PASS \
              M142_ORIGINAL_NPING_UDP_COMMAND_PASS \
              M142_DNS_UPDATE_AND_NPING_WIRE_PASS; do
    grep -Fxq "$marker" "$clean" || {
        echo "[M142 DDNS WIRE FAIL] guest missing $marker" >&2; exit 1;
    }
done
for program in nsupdate host nping; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$program" "$clean" || {
        echo "[M142 DDNS WIRE FAIL] original $program ELF did not exec" >&2;
        exit 1;
    }
done
# A successful guest exit or a lone CLI PASS is not packet proof. Require the
# independent host to parse, ACK and persist the RFC2136 UPDATE, answer the
# post-update query, and receive Nping's exact unprivileged UDP payload.
for evidence in \
    '[M142 DNS HOST UPDATE] naseed.test. updated.naseed.test. A 192.0.2.99 ttl=60' \
    '[M142 DNS HOST ACK] RFC2136 NOERROR' \
    '[M142 DNS HOST QUERY] post-update A updated.naseed.test.' \
    '[M142 DNS HOST ANSWER] 192.0.2.99' \
    '[M142 DNS HOST NPING] exact unprivileged UDP probe'; do
    grep -Fq "$evidence" "$root/host-m142-dns-update.log" || {
        echo "[M142 DDNS WIRE FAIL] host packet evidence missing: $evidence" >&2;
        exit 1;
    }
done
echo '[M142 DNS UPDATE QEMU PASS] signed Alpine nsupdate RFC2136 + host state query + Nping UDP over real VirtIO/DHCP'
