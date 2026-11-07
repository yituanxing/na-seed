#!/bin/sh
# Genuine signed Alpine 3.23 network/security portfolio on native NaSeed QEMU.
set -eu
[ "$#" -le 1 ] || { echo 'usage: sh scripts/probe-m149-network-cohort.sh [amd64|riscv64|arm64]' >&2; exit 2; }
port=${1:-amd64}
. scripts/native-qemu-target.sh
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE
export NAS_APP_BATCH_QEMU_TIMEOUT=540
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m149-network-cases.tsv
sh scripts/stage-m149-prebuilt.sh "$port"
root="build/external/m149-batch/$port"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/bin/busybox"
test -s "$root/m149-original-elfs.sha256"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m149-virtio-wire.pcap"
case "$port" in
  riscv64) sh scripts/build-riscv64-full.sh ;;
  arm64) sh scripts/build-aarch64-full.sh ;;
esac
python3 -u scripts/m149-http-oracle.py > "$root/host-m149-http.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5 6 7 8; do
  if grep -Fq '[M149 HOST HTTP READY]' "$root/host-m149-http.log"; then
    ready=true; break
  fi
  sleep 1
done
[ "$ready" = true ] || { cat "$root/host-m149-http.log" >&2; exit 1; }
clean="$logs/m149-batch.clean.log"
if ! sh "$runner"; then
  echo "[M149 FAIL] $port genuine QEMU network cohort" >&2
  if test -s "$clean"; then
    grep -aE '^(M149_|\[M149 |\[LWIP |\[VIRTIO-NET)' "$clean" >&2 || :
    tail -180 "$clean" >&2
  fi
  exit 1
fi
test -s "$clean"
case "$port" in
  riscv64|arm64)
    grep -Fq '[VIRTIO-NET] real modern MMIO devices=1' "$clean" || {
      echo "[M149 FAIL] $port real MMIO NIC missing" >&2; exit 1;
    } ;;
esac
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
  echo '[M149 FAIL] genuine DHCP lease missing' >&2; exit 1;
}
for marker in M149_35_REAL_ELF_LOAD_PASS M149_REAL_SNMP_UDP_AGENT_PASS \
              M149_REAL_TINYPROXY_HTTP_FORWARD_PASS M149_NETWORK_COHORT_PASS; do
  grep -Fxq "$marker" "$clean" || {
    echo "[M149 FAIL] $port missing exact guest semantic marker: $marker" >&2; exit 1;
  }
done
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
    echo "[M149 FAIL] $port real guest ELF never executed: /$elf" >&2; exit 1;
  }
done < scripts/m149-original-elfs.txt
for other in /usr/bin/curl /usr/sbin/snmpd /usr/bin/snmpget \
             /usr/bin/snmpwalk /usr/bin/tinyproxy; do
  grep -Fq "[EXEC] replaced process with CPIO ELF: $other" "$clean" || {
    echo "[M149 FAIL] $port original client/server missing: $other" >&2; exit 1;
  }
done
grep -Fq '[M149 HOST PROXY GET] /proxy' "$root/host-m149-http.log" || {
  echo '[M149 FAIL] independent host saw no actual proxied GET' >&2; exit 1;
}
echo "[M149 QEMU PASS] $port 35 new original signed ELF + SNMP UDP + actual host HTTP via guest Tinyproxy"
