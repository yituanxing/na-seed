#!/bin/sh
# Real native QEMU M152: 35 signed ELF + DNS forwarding + SMTP wire delivery.
set -eu
[ "$#" -le 1 ] || { echo 'usage: sh scripts/probe-m152-network-cohort.sh [amd64|riscv64|arm64]' >&2; exit 2; }
port=${1:-amd64}
. scripts/native-qemu-target.sh
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE
export NAS_APP_BATCH_QEMU_TIMEOUT=540
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m152-network-cases.tsv
sh scripts/stage-m152-prebuilt.sh "$port"
root="build/external/m152-batch/$port"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/bin/busybox"
test -s "$root/m152-original-elfs.sha256"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m152-virtio-wire.pcap"
case "$port" in
  riscv64) sh scripts/build-riscv64-full.sh ;;
  arm64) sh scripts/build-aarch64-full.sh ;;
esac
python3 -u scripts/m152-host-oracle.py > "$root/host-m152-protocols.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5 6 7 8; do
  if grep -Fq '[M152 HOST PROTOCOLS READY]' "$root/host-m152-protocols.log"; then
    ready=true; break
  fi
  sleep 1
done
[ "$ready" = true ] || { cat "$root/host-m152-protocols.log" >&2; exit 1; }
clean="$logs/m152-batch.clean.log"
if ! sh "$runner"; then
  echo "[M152 FAIL] $port native QEMU network cohort" >&2
  if test -s "$clean"; then
    grep -aE '^(M152_|\[M152 |\[LWIP |\[VIRTIO-NET)' "$clean" >&2 || :
    tail -160 "$clean" >&2
  fi
  cat "$root/host-m152-protocols.log" >&2 || :
  exit 1
fi
test -s "$clean"
test -s "$root/m152-virtio-wire.pcap" || {
  echo '[M152 FAIL] no actual QEMU VirtIO network packet capture' >&2; exit 1;
}
case "$port" in
  amd64)
    grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || exit 1 ;;
  riscv64|arm64)
    grep -Fq '[VIRTIO-NET] real modern MMIO devices=1' "$clean" || exit 1 ;;
esac
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
  echo '[M152 FAIL] no actual guest DHCP lease' >&2; exit 1;
}
for marker in M152_35_REAL_ELF_LOAD_PASS M152_REAL_LDNS_ZONE_PARSE_PASS \
              M152_REAL_DNS_FORWARD_PASS M152_REAL_SMTP_HOST_DELIVERY_PASS \
              M152_NETWORK_COHORT_PASS; do
  grep -Fxq "$marker" "$clean" || {
    echo "[M152 FAIL] $port missing guest semantic marker: $marker" >&2; exit 1;
  }
done
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  grep -Fxq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
    echo "[M152 FAIL] $port never executed original ELF: /$elf" >&2; exit 1;
  }
done < scripts/m152-original-elfs.txt
for elf in /usr/sbin/dnsmasq /usr/bin/drill /usr/bin/msmtp; do
  grep -Fxq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
    echo "[M152 FAIL] $port original DNS/SMTP server/client absent: $elf" >&2; exit 1;
  }
done
grep -Fxq '[M152 HOST DNS ANSWER] m152.example.test A 198.51.100.152' \
  "$root/host-m152-protocols.log" || {
    echo '[M152 FAIL] independent host DNS oracle answered no matching wire query' >&2; exit 1;
}
grep -Eq '^\[M152 HOST SMTP DELIVERED\] subject=M152-SMTP-WIRE bytes=[0-9]+$' \
  "$root/host-m152-protocols.log" || {
    echo '[M152 FAIL] independent host SMTP oracle accepted no verified DATA' >&2; exit 1;
}
echo "[M152 QEMU PASS] $port 35 new signed ELF + LDNS zone + real DNS UDP and SMTP TCP host wire"
