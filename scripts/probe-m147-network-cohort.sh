#!/bin/sh
# One x86-64 native QEMU boot: 29 signed independent Alpine ELF admissions
# plus real TCP memcached, MQTT pub/sub and HTTP/2 h2c loopback transactions.
set -eu
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE
export NAS_APP_BATCH_QEMU_TIMEOUT=540
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m147-network-cases.tsv
sh scripts/stage-m147-prebuilt.sh amd64
root=build/external/m147-batch/amd64
test -s "$root/rootfs.cpio" && test -s "$root/m147-original-elfs.sha256"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m147-virtio-wire.pcap"
sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m147-batch.clean.log
test -s "$clean"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
  echo '[M147 FAIL] real DHCP lease not observed' >&2; exit 1;
}
for marker in M147_29_REAL_ELF_LOAD_PASS M147_REAL_MEMCACHED_TCP_PASS \
              M147_REAL_MQTT_PUBSUB_PASS M147_REAL_HTTP2_H2C_PASS \
              M147_NETWORK_COHORT_PASS; do
  grep -Fxq "$marker" "$clean" || {
    echo "[M147 FAIL] missing exact guest semantic marker: $marker" >&2; exit 1;
  }
done
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
    echo "[M147 FAIL] never entered genuine guest ELF: /$elf" >&2; exit 1;
  }
done < scripts/m147-original-elfs.txt
for other in /usr/bin/ncat /usr/sbin/mosquitto /usr/bin/mosquitto_pub \
             /usr/bin/mosquitto_sub /usr/bin/memcached \
             /usr/bin/nghttpd /usr/bin/nghttp; do
  grep -Fq "[EXEC] replaced process with CPIO ELF: $other" "$clean" || {
    echo "[M147 FAIL] real client/server not executed: $other" >&2; exit 1;
  }
done
echo '[M147 QEMU PASS] 29 new signed original network ELFs + memcached/MQTT/HTTP2 actual loopback protocols'
