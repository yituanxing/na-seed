#!/bin/sh
# Signed original Alpine curl and GNU wget perform certificate-verified TLS
# over the real QEMU PCI VirtIO-Net/lwIP path, with a separate host oracle.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-m156-https-wire-smoke.sh' >&2; exit 2; }
command -v openssl >/dev/null || {
    echo '[M156 HTTPS FAIL] host openssl CLI unavailable' >&2
    exit 1
}
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m156-https-cases.tsv

# This is a catalog-backed profile of the SAME M143 signed Alpine curl/wget
# packages, not a claim of new ELF identities. The shared overlay is the only
# CPIO writer; its profile-local host hook creates ephemeral TLS keys/CA.
sh scripts/stage-m156-prebuilt.sh amd64
root=build/external/m156-https/amd64
test -s "$root/rootfs/usr/bin/curl"
test -s "$root/rootfs/usr/bin/wget"
test -s "$root/rootfs/tests/m156-https-run.sh"
test -s "$root/rootfs/tests/m156-https-ca.pem"
test -s "$root/host-ca.crt" && test -s "$root/host-tls.crt"
test -s "$root/rootfs.cpio"
# These are the two previously admitted M143 signed Alpine executables,
# independently hashed here to exclude a BusyBox/host-binary substitution.
for elf in curl wget; do
    file="$root/rootfs/usr/bin/$elf"
    test -f "$file" && test -x "$file"
    readelf -h "$file" | grep -Fq 'Advanced Micro Devices X86-64'
    case "$(readlink -f "$file")" in
        "$(pwd)/$root/rootfs"/*) ;;
        *) echo "[M156 HTTPS FAIL] ELF escaped signed root: $elf" >&2; exit 1 ;;
    esac
done
sha256sum "$root/rootfs/usr/bin/curl" "$root/rootfs/usr/bin/wget" \
    > "$root/m156-original-signed-elfs.sha256"
test "$(awk '{print $1}' "$root/m156-original-signed-elfs.sha256" | sort -u | wc -l)" -eq 2
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-m156-https.pcap"

python3 -u scripts/m156-https-wire-server.py \
    "$root/host-tls.crt" "$root/host-tls.key" \
    > "$root/host-m156-https.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[M156 HTTPS HOST READY]' "$root/host-m156-https.log"; then
        ready=true
        break
    fi
    sleep 1
done
[ "$ready" = true ] || {
    echo '[M156 HTTPS FAIL] host TLS oracle unavailable' >&2
    sed -n '1,80p' "$root/host-m156-https.log" >&2
    exit 1
}

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m156-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean"
for marker in M156_REAL_CURL_VERIFIED_TLS_PASS \
              M156_REAL_WGET_VERIFIED_TLS_PASS \
              M156_HTTPS_WIRE_PASS; do
    grep -Fxq "$marker" "$clean" || {
        echo "[M156 HTTPS FAIL] missing guest marker: $marker" >&2
        exit 1
    }
done
for elf in /usr/bin/curl /usr/bin/wget; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
        echo "[M156 HTTPS FAIL] original Alpine ELF was not run: $elf" >&2
        exit 1
    }
done
for path in curl wget; do
    case "$path" in curl) payload_bytes=29 ;; wget) payload_bytes=29 ;; esac
    count=$(grep -Ec "^\\[M156 HTTPS HOST GET\\] /$path bytes=$payload_bytes tls=TLSv1\\.[23]$" \
        "$root/host-m156-https.log" || :)
    [ "$count" -eq 1 ] || {
        echo "[M156 HTTPS FAIL] expected exactly one verified TLS GET for $path, observed $count" >&2
        sed -n '1,120p' "$root/host-m156-https.log" >&2
        exit 1
    }
done
echo '[M156 HTTPS QEMU PASS] original signed Alpine curl+wget verified TLS 1.2+ certificates and HTTP bodies through real VirtIO/lwIP'
