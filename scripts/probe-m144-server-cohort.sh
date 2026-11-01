#!/bin/sh
set -eu
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE # The production-style gate must not rely on debug tracing.
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=2048M BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m144-server-cases.tsv
sh scripts/stage-m144-prebuilt.sh amd64
root=build/external/m144-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m144-original-elfs.sha256"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m144-virtio-wire.pcap"
python3 -u scripts/m144-http-oracle.py > "$root/host-m144-http.log" 2>&1 &
hostpid=$!
trap 'kill "$hostpid" 2>/dev/null || :; wait "$hostpid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5; do
  if grep -Fq '[M144 HOST READY]' "$root/host-m144-http.log"; then
    ready=true; break
  fi
  sleep 1
done
[ "$ready" = true ]
sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m144-batch.clean.log
test -s "$clean"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean"
for marker in M144_SIGNED_AB_PASS M144_SIGNED_NGINX_PASS \
              M144_SIGNED_HAPROXY_PASS M144_SIGNED_VALKEY_SERVER_CLI_PASS \
              M144_SERVER_COHORT_PASS; do
    grep -Fxq "$marker" "$clean" || { echo "[M144 FAIL] missing $marker"; exit 1; }
done
for elf in /usr/bin/ab /usr/sbin/nginx /usr/sbin/haproxy \
           /usr/bin/valkey-server /usr/bin/valkey-cli; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
      echo "[M144 FAIL] no real signed execution: $elf"; exit 1;
    }
done
count=$(grep -Fc '[M144 HOST GET] /ab ' "$root/host-m144-http.log" || :)
[ "$count" -eq 4 ] || { echo "[M144 FAIL] ab requests: $count"; exit 1; }
grep -Fq '[M144 HOST GET] /haproxy ' "$root/host-m144-http.log"
echo '[M144 QEMU PASS] signed Alpine ab, nginx, HAProxy, Valkey server/client with true network + RESP'
