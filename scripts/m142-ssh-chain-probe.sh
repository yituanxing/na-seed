#!/bin/sh
# Ephemeral test identities, real OpenSSH server and three signed Alpine
# clients in ONE NaSeed VirtIO-Net/lwIP QEMU boot. No persistent user keys.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/m142-ssh-chain-probe.sh' >&2; exit 2; }
PATH=/usr/sbin:/sbin:$PATH
export PATH
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m142-ssh-chain-cases.tsv

command -v sshd >/dev/null
command -v ssh-keygen >/dev/null
sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
for elf in ssh scp sftp; do test -s "$root/rootfs/usr/bin/$elf"; done
test -s "$root/rootfs/tests/m142-ssh-chain-run.sh"
test -s "$root/rootfs/bin/busybox"

root_abs=$(cd "$root" && pwd -P)
host_dir="$root_abs/host-ssh-chain"
guest_dir="$root/rootfs/tests/ssh-chain"
host_user=$(id -un)
case "$host_user" in ''|*[!a-zA-Z0-9_-]*) exit 1 ;; esac
umask 077
mkdir -p "$host_dir" "$guest_dir"
chmod 700 "$host_dir" "$guest_dir"
host_server_pid=
cleanup() {
    status=$?
    if [ -n "$host_server_pid" ]; then
        kill "$host_server_pid" 2>/dev/null || :
        wait "$host_server_pid" 2>/dev/null || :
    fi
    # Both guest-side secret copies, the private host identities, and the
    # temporary CPIO containing a client secret are removed on ANY exit.
    rm -f "$guest_dir/id_ed25519" "$guest_dir/known_hosts" \
       "$guest_dir/host-info" "$root/rootfs-ssh-chain.cpio" \
       "$root/rootfs-ssh-chain.cpio.tmp" \
       "$host_dir/client_id_ed25519" "$host_dir/client_id_ed25519.pub" \
       "$host_dir/ssh_host_ed25519_key" "$host_dir/authorized_keys"
    exit "$status"
}
trap cleanup EXIT

ssh-keygen -q -t ed25519 -N '' -C naseed-temporary-client \
    -f "$host_dir/client_id_ed25519"
ssh-keygen -q -t ed25519 -N '' -C naseed-temporary-server \
    -f "$host_dir/ssh_host_ed25519_key"
cp "$host_dir/client_id_ed25519.pub" "$host_dir/authorized_keys"
chmod 600 "$host_dir/authorized_keys"
ssh-keygen -lf "$host_dir/ssh_host_ed25519_key.pub" -E sha256 \
    > "$root/host-ssh-chain-fingerprint.log"

# The host public key is pinned in the ephemeral guest image. Only the
# freshly generated *client* private key enters this single-use CPIO.
cp "$host_dir/client_id_ed25519" "$guest_dir/id_ed25519"
chmod 600 "$guest_dir/id_ed25519"
awk '$1=="ssh-ed25519" {print "[10.0.2.2]:15423",$1,$2}' \
    "$host_dir/ssh_host_ed25519_key.pub" > "$guest_dir/known_hosts"
printf '%s %s\n' "$host_user" "$host_dir" > "$guest_dir/host-info"
test -s "$guest_dir/known_hosts"

# The minimal Alpine CPIO does not install account metadata. OpenSSH's real
# client calls getpwuid(getuid()) before connecting; without uid 0 in passwd
# it exits 255 with "No user exists for uid 0". Supply only an ephemeral
# local root identity in this test-specific CPIO, not a synthetic SSH client.
mkdir -p "$root/rootfs/etc"
if [ ! -s "$root/rootfs/etc/passwd" ]; then
    printf 'root:x:0:0:root:/tmp/m142-ssh-chain:/bin/sh\n' \
        > "$root/rootfs/etc/passwd"
fi
if [ ! -s "$root/rootfs/etc/group" ]; then
    printf 'root:x:0:\n' > "$root/rootfs/etc/group"
fi

# Repack only for this acceptance case, with the same canonical newc order
# as stage-alpine-prebuilt-overlay.sh. Do not mutate the signed ELF payloads
# or the ordinary rootfs.cpio reused by other independent smoke gates.
(
    cd "$root/rootfs"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || exit 1
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$root/rootfs-ssh-chain.cpio.tmp"
mv "$root/rootfs-ssh-chain.cpio.tmp" "$root/rootfs-ssh-chain.cpio"
test -s "$root/rootfs-ssh-chain.cpio"
export NAS_ACCEPTANCE_CPIO="$root/rootfs-ssh-chain.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-ssh-chain-wire.pcap"

# The host sources are unpredictable and never enter guest CPIO. Guest SCP
# and SFTP must download them and upload a second exact copy back to host.
nonce=$(od -An -tx1 -N16 /dev/urandom | tr -d ' \n')
printf 'M142_HOST_SCP_SOURCE_%s\n' "$nonce" > "$host_dir/scp-source"
printf 'M142_HOST_SFTP_SOURCE_%s\n' "$nonce" > "$host_dir/sftp-source"

# Isolated high-port OpenSSH daemon: only loopback is bound, only this
# temporary client key is authorized, no password/PAM/agent or forwarding.
# SFTP is the real OpenSSH internal subsystem (not a guest test stub).
sshd_bin=$(command -v sshd)
"$sshd_bin" -D -e -f /dev/null -p 15423 \
    -h "$host_dir/ssh_host_ed25519_key" \
    -o ListenAddress=127.0.0.1 \
    -o HostKeyAlgorithms=ssh-ed25519 \
    -o PubkeyAuthentication=yes \
    -o "AuthorizedKeysFile=$host_dir/authorized_keys" \
    -o PasswordAuthentication=no \
    -o KbdInteractiveAuthentication=no \
    -o HostbasedAuthentication=no \
    -o PermitRootLogin=no \
    -o UsePAM=no \
    -o AllowTcpForwarding=no \
    -o PermitTunnel=no \
    -o X11Forwarding=no \
    -o PermitTTY=no \
    -o 'Subsystem=sftp internal-sftp' \
    -o LogLevel=VERBOSE \
    -o "PidFile=$host_dir/sshd.pid" \
    > "$root/host-ssh-chain.log" 2>&1 &
host_server_pid=$!
ready=false
for attempt in 1 2 3 4 5; do
    if python3 -c '
import socket
with socket.create_connection(("127.0.0.1", 15423), 2) as sock:
    sock.settimeout(2)
    assert sock.recv(160).startswith(b"SSH-2.0-OpenSSH_")
' >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 1
done
[ "$ready" = true ] || {
    echo '[M142 SSH CHAIN FAIL] isolated host sshd never provided SSH banner' >&2
    exit 1
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean"
for marker in M142_SIGNED_SSH_COMMAND_PASS \
              M142_SIGNED_SCP_ROUNDTRIP_PASS \
              M142_SIGNED_SFTP_ROUNDTRIP_PASS \
              M142_SIGNED_SSH_CHAIN_CASE_PASS; do
    grep -Fxq "$marker" "$clean" || {
        echo "[M142 SSH CHAIN FAIL] missing genuine guest marker: $marker" >&2
        exit 1
    }
done

# Host bytes prove successful SCP/SFTP transfers independently of guest
# printing. The server also must record a real temporary-key login.
grep -Eq "Accepted publickey for $host_user" "$root/host-ssh-chain.log" || {
    echo '[M142 SSH CHAIN FAIL] host recorded no public-key authentication' >&2
    exit 1
}
grep -Fxq M142_REAL_SCP_GUEST_UPLOAD "$host_dir/scp-up"
cmp "$host_dir/scp-source" "$host_dir/scp-echo"
grep -Fxq M142_REAL_SFTP_GUEST_UPLOAD "$host_dir/sftp-up"
cmp "$host_dir/sftp-source" "$host_dir/sftp-echo"
echo '[M142 SSH CHAIN QEMU PASS] signed Alpine SSH command, SCP round-trip, SFTP round-trip over real VirtIO TCP and isolated OpenSSH keys'
