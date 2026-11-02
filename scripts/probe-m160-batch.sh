#!/bin/sh
# Real host HTTPS PNG -> guest original Alpine curl/wget -> actual PNG pixels
# -> original Alpine chafa serial rendering across three native QEMU ISAs.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m160-batch.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64'; nic=PCI ;;
    riscv64) machine='RISC-V'; nic=MMIO ;;
    arm64) machine='AArch64'; nic=MMIO ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh
command -v openssl >/dev/null || exit 1
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_APP_BATCH_QEMU_TIMEOUT=600
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m160-batch-cases.tsv
unset NAS_SYSCALL_TRACE

sh scripts/stage-m160-prebuilt.sh "$port"
work="build/external/m160-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
test -s "$work/host-m160-image.png"
for elf in curl wget pngtopam pamfile pamtable chafa; do
    binary="$root/usr/bin/$elf"
    test -x "$binary" || exit 1
    readelf -h "$binary" | grep -Fq "$machine" || {
        echo "[M160 FAIL] wrong native ISA $port /usr/bin/$elf" >&2
        exit 1
    }
done
export BUSYBOX_PARITY_BINARY="$root/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_VIRTIO_NET_PCAP="$work/virtio-m160-image.pcap"
case "$port" in
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64) sh scripts/build-aarch64-full.sh ;;
esac

# Reuse M156's independently tested TLS Handler; the M160 server only adds
# the actual PNG body. Host-only private keys remain outside guest/rootfs.
python3 -u scripts/m160-image-wire-server.py \
    "$work/host-tls.crt" "$work/host-tls.key" \
    "$work/host-m160-image.png" > "$work/host-m160-image.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5 6 7 8; do
    if grep -Fq '[M160 IMAGE HOST READY]' "$work/host-m160-image.log"; then
        ready=true
        break
    fi
    sleep 1
done
[ "$ready" = true ] || {
    echo '[M160 FAIL] independent host TLS image oracle unavailable' >&2
    sed -n '1,100p' "$work/host-m160-image.log" >&2
    exit 1
}

sh "$runner"
clean="$logs/m160-batch.clean.log"
test -s "$clean" || exit 1
grep -Fq "[VIRTIO-NET] real modern $nic devices=1" "$clean" || {
    echo "[M160 FAIL] $port real $nic VirtIO NIC not attached" >&2
    exit 1
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
    echo "[M160 FAIL] $port no authentic DHCP lease" >&2
    exit 1
}
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    count=$(grep -Fxc "$marker" "$clean" || :)
    [ "$count" -eq 1 ] || {
        echo "[M160 FAIL] $port $marker count=$count" >&2
        tail -300 "$clean" >&2
        exit 1
    }
done < scripts/m160-expected-markers.txt
while IFS= read -r path; do
    case "$path" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$path" "$clean" || {
        echo "[M160 FAIL] original Alpine ELF not executed: /$path" >&2
        exit 1
    }
done < scripts/m160-reused-elfs.txt

# Exactly two authenticated, live HTTP GETs, with original PNG bytes; the
# guest cannot pass by replaying a host-staged image from its CPIO archive.
payload_bytes=$(wc -c < "$work/host-m160-image.png" | tr -d ' ')
for path in m160-curl.png m160-wget.png; do
    count=$(grep -Ec "^\\[M156 HTTPS HOST GET\\] /$path bytes=$payload_bytes tls=TLSv1\\.[23]$" \
        "$work/host-m160-image.log" || :)
    [ "$count" -eq 1 ] || {
        echo "[M160 FAIL] $port expected one authentic TLS GET /$path, count=$count" >&2
        sed -n '1,100p' "$work/host-m160-image.log" >&2
        exit 1
    }
done
test "$(grep -Fc '[M156 HTTPS HOST GET]' "$work/host-m160-image.log" || :)" -eq 2 || exit 1
echo "[M160 NATIVE QEMU PASS] $port original signed HTTPS PNG download, pixel decode and terminal image render over $nic VirtIO/lwIP"
