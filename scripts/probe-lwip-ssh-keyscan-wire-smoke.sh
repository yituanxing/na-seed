#!/bin/sh
# Original signed Alpine ssh-keyscan reads a REAL temporary OpenSSH host key
# over the guest's PCI VirtIO-Net/lwIP TCP socket, then a host oracle compares
# exact public-key bytes. No guest key fixture, stub sshd or login is used.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-ssh-keyscan-wire-smoke.sh' >&2; exit 2; }
PATH=/usr/sbin:/sbin:$PATH
export PATH
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-ssh-keyscan-wire-cases.tsv

command -v sshd >/dev/null || {
    echo '[LWIP SSH WIRE FAIL] host OpenSSH sshd unavailable' >&2; exit 1;
}
command -v ssh-keygen >/dev/null || {
    echo '[LWIP SSH WIRE FAIL] host OpenSSH ssh-keygen unavailable' >&2; exit 1;
}
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/usr/bin/ssh-keyscan"
test -s "$root/rootfs/tests/m141-ssh-keyscan-wire-run.sh"
test -s "$root/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-ssh-keyscan-wire.pcap"

# Ephemeral test-only ED25519 host identity: the private key stays in this
# job's private staging directory and is NEVER included in CI artifacts.
# The sshd is deliberately loopback-only, has no authentication mechanisms,
# no remote forwarding and ignores the machine's normal sshd_config.
host_dir="$PWD/$root/host-ssh-keyscan" # sshd -h requires an absolute key path
umask 077
mkdir -p "$host_dir"
chmod 700 "$host_dir"
ssh-keygen -q -t ed25519 -N '' -C naseed-wire-test \
    -f "$host_dir/ssh_host_ed25519_key"
ssh-keygen -lf "$host_dir/ssh_host_ed25519_key.pub" -E sha256 \
    > "$root/host-ssh-fingerprint.log"

sshd_bin=$(command -v sshd)
"$sshd_bin" -D -e -f /dev/null -p 15422 \
  -h "$host_dir/ssh_host_ed25519_key" \
  -o ListenAddress=127.0.0.1 \
  -o HostKeyAlgorithms=ssh-ed25519 \
  -o PasswordAuthentication=no \
  -o PubkeyAuthentication=no \
  -o KbdInteractiveAuthentication=no \
  -o HostbasedAuthentication=no \
  -o PermitRootLogin=no \
  -o UsePAM=no \
  -o AllowTcpForwarding=no \
  -o PermitTunnel=no \
  -o X11Forwarding=no \
  -o PermitTTY=no \
  -o "PidFile=$host_dir/sshd.pid" \
  > "$root/host-sshd.log" 2>&1 &
host_server_pid=$!
trap 'kill "$host_server_pid" 2>/dev/null || :; wait "$host_server_pid" 2>/dev/null || :' EXIT

host_ready=false
for attempt in 1 2 3 4 5; do
    if python3 -c '
import socket
with socket.create_connection(("127.0.0.1", 15422), 2) as sock:
    sock.settimeout(2)
    assert sock.recv(160).startswith(b"SSH-2.0-OpenSSH_")
' >/dev/null 2>&1; then
        host_ready=true
        break
    fi
    sleep 1
done
[ "$host_ready" = true ] || {
    echo '[LWIP SSH WIRE FAIL] isolated OpenSSH host banner unavailable' >&2
    exit 1
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP SSH WIRE FAIL] no real PCI VirtIO NIC' >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo '[LWIP SSH WIRE FAIL] no actual DHCP lease' >&2; exit 1;
}
grep -Fxq M141_SIGNED_SSH_KEYSCAN_WIRE_PASS "$clean" || {
    echo '[LWIP SSH WIRE FAIL] original Alpine ssh-keyscan got no ED25519 key' >&2; exit 1;
}
grep -Fxq M141_SIGNED_SSH_KEYSCAN_CASE_PASS "$clean" || {
    echo '[LWIP SSH WIRE FAIL] guest script did not exit cleanly' >&2; exit 1;
}

# Compare the EXACT key generated after staging with the text emitted by
# the real guest scanner; even a plausible SSH banner or arbitrary key is
# insufficient. There is no host public key in the guest CPIO image.
host_key=$(awk '$1=="ssh-ed25519" {print $2}' \
    "$host_dir/ssh_host_ed25519_key.pub")
guest_key=$(awk '$1=="M141_SIGNED_SSH_KEYSCAN_PUBLIC" && NF==2 {print $2}' \
    "$clean" | sort -u)
[ -n "$host_key" ] && [ "$guest_key" = "$host_key" ] || {
    echo '[LWIP SSH WIRE FAIL] guest public key mismatches temporary host identity' >&2
    exit 1
}
echo '[LWIP VIRTIO SSH KEYSCAN QEMU PASS] real PCI NIC, DHCP, signed Alpine OpenSSH public key matches host ED25519 fingerprint'
