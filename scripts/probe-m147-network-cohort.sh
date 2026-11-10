#!/bin/sh
# One native system-QEMU boot per ISA: the same 29 signed independent Alpine
# ELFs plus real TCP memcached, MQTT pub/sub and HTTP/2 h2c transactions.
set -eu
[ "$#" -le 1 ] || {
  echo 'usage: sh scripts/probe-m147-network-cohort.sh [amd64|riscv64|arm64]' >&2
  exit 2
}
port=${1:-amd64}
. scripts/native-qemu-target.sh
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE
export NAS_APP_BATCH_QEMU_TIMEOUT=540
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m147-network-cases.tsv
sh scripts/stage-m147-prebuilt.sh "$port"
root="build/external/m147-batch/$port"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/bin/busybox" &&
  test -s "$root/m147-original-elfs.sha256"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m147-virtio-wire.pcap"
# Reuse the normal production kernel, exact guest manifest and QEMU runner.
# The generic APK staging owner already checks every ELF's target machine.
case "$port" in
  riscv64) sh scripts/build-riscv64-full.sh ;;
  arm64) sh scripts/build-aarch64-full.sh ;;
esac
clean="$logs/m147-batch.clean.log"
if ! sh "$runner"; then
  echo "[M147 FAIL] $port native QEMU original software/protocol run" >&2
  if test -s "$clean"; then
    grep -aE '^(M147_|\[M147 |\[LWIP |\[VIRTIO-NET)' "$clean" >&2 || :
    tail -180 "$clean" >&2
  fi
  exit 1
fi
test -s "$clean"
case "$port" in
  riscv64|arm64)
    grep -Fq '[VIRTIO-NET] real modern MMIO devices=1' "$clean" || {
      echo "[M147 FAIL] $port did not attach native modern VirtIO-MMIO NIC" >&2; exit 1;
    } ;;
esac
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
  echo "[M147 FAIL] $port real DHCP lease not observed" >&2; exit 1;
}
for marker in M147_29_REAL_ELF_LOAD_PASS M147_REAL_MEMCACHED_TCP_PASS \
              M147_REAL_MQTT_PUBSUB_PASS M147_REAL_HTTP2_H2C_PASS \
              M147_NETWORK_COHORT_PASS; do
  grep -Fxq "$marker" "$clean" || {
    echo "[M147 FAIL] $port missing exact guest semantic marker: $marker" >&2; exit 1;
  }
done
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
    echo "[M147 FAIL] $port never entered genuine guest ELF: /$elf" >&2; exit 1;
  }
done < scripts/m147-original-elfs.txt
for other in /usr/bin/ncat /usr/sbin/mosquitto /usr/bin/mosquitto_pub \
             /usr/bin/mosquitto_sub /usr/bin/memcached \
             /usr/bin/nghttpd /usr/bin/nghttp; do
  grep -Fq "[EXEC] replaced process with CPIO ELF: $other" "$clean" || {
    echo "[M147 FAIL] $port real client/server not executed: $other" >&2; exit 1;
  }
done
echo "[M147 QEMU PASS] $port 29 new signed original network ELFs + memcached/MQTT/HTTP2 actual loopback protocols"
