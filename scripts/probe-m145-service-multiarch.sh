#!/bin/sh
# M145: run previously x86-proven ORIGINAL signed Alpine M143/M144 workloads
# on native RV64 and ARM64 QEMU VirtIO-MMIO; no synthetic guest equivalents.
# Each port/cohort combination boots once with the same workload manifest and
# guest scripts used by the x86 acceptance gates.
set -eu
[ "$#" -eq 2 ] || {
  echo 'usage: sh scripts/probe-m145-service-multiarch.sh riscv64|arm64 m143|m144' >&2
  exit 2
}
port=$1 cohort=$2
case "$port" in riscv64|arm64) ;; *) exit 2 ;; esac
case "$cohort" in m143|m144) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
unset NAS_SYSCALL_TRACE
export NAS_APP_BATCH_QEMU_TIMEOUT=540
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export BUSYBOX_PARITY_EXPECTED_COUNT=1
case "$cohort" in
  m143)
    profile=m143-batch
    export BUSYBOX_PARITY_MANIFEST=scripts/m143-http-cases.tsv
    host_program=scripts/m143-http-server.py
    host_ready='[M143 HOST HTTP READY]'
    host_log=host-m143-http.log
    expected_hash_file=m143-original-signed-elfs.sha256
    ;;
  m144)
    profile=m144-batch
    export BUSYBOX_PARITY_MANIFEST=scripts/m144-server-cases.tsv
    host_program=scripts/m144-http-oracle.py
    host_ready='[M144 HOST READY]'
    host_log=host-m144-http.log
    expected_hash_file=m144-original-elfs.sha256
    ;;
esac
# All ELF identities are the host-verified originals, with the actual
# architecture's machine type confirmed by shared APK staging.
sh "scripts/stage-$cohort-prebuilt.sh" "$port"
root="build/external/$profile/$port"
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/bin/busybox"
test -s "$root/$expected_hash_file"
test -s "$root/rootfs/tests/$cohort-$(test "$cohort" = m143 && printf http || printf server-batch)-run.sh"
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/m145-$cohort-wire.pcap"
# Multiarch runner intentionally reuses the exact standard kernel build and
# shared three-ISA original-BusyBox QEMU harness, not a test-only kernel.
case "$port" in
  riscv64) sh scripts/build-riscv64-full.sh ;;
  arm64) sh scripts/build-aarch64-full.sh ;;
esac
python3 -u "$host_program" > "$root/$host_log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
host_ok=false
for i in 1 2 3 4 5 6 7 8; do
  if grep -Fq "$host_ready" "$root/$host_log"; then host_ok=true; break; fi
  sleep 1
done
[ "$host_ok" = true ] || { cat "$root/$host_log" >&2; exit 1; }

sh "$runner"
clean="$logs/$profile.clean.log"
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern MMIO devices=1' "$clean" || {
  echo "[M145 FAIL] $port/$cohort original MMIO NIC not attached" >&2; exit 1;
}
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean" || {
  echo "[M145 FAIL] $port/$cohort genuine DHCP lease absent" >&2; exit 1;
}
case "$cohort" in
  m143)
    for marker in M143_REAL_HTTP_WGET_DOWNLOAD_PASS \
                  M143_REAL_HTTP_ARIA2_DOWNLOAD_PASS \
                  M143_REAL_HTTP_CURL_DOWNLOAD_PASS \
                  M143_REAL_OPENSSL_SHA256_PASS \
                  M143_REAL_LIGHTTPD_LOOPBACK_PASS \
                  M143_HTTP_COHORT_PASS; do
      grep -Fxq "$marker" "$clean" || {
        echo "[M145 FAIL] $port missing original guest marker $marker" >&2; exit 1;
      }
    done
    for elf in /usr/bin/wget /usr/bin/aria2c /usr/sbin/lighttpd \
               /usr/bin/curl /usr/bin/openssl; do
      grep -Fq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
        echo "[M145 FAIL] $port original ELF not executed: $elf" >&2; exit 1;
      }
    done
    for entry in '/wget bytes=28' '/aria2 bytes=29' '/curl bytes=28'; do
      grep -Fq "[M143 HOST HTTP GET] $entry" "$root/$host_log" || {
        echo "[M145 FAIL] $port host did not receive authentic GET $entry" >&2; exit 1;
      }
    done
    ;;
  m144)
    for marker in M144_SIGNED_AB_PASS M144_SIGNED_NGINX_PASS \
                  M144_SIGNED_HAPROXY_PASS \
                  M144_SIGNED_VALKEY_SERVER_CLI_PASS \
                  M144_SERVER_COHORT_PASS; do
      grep -Fxq "$marker" "$clean" || {
        echo "[M145 FAIL] $port missing original guest marker $marker" >&2; exit 1;
      }
    done
    for elf in /usr/bin/ab /usr/sbin/nginx /usr/sbin/haproxy \
               /usr/bin/valkey-server /usr/bin/valkey-cli; do
      grep -Fq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
        echo "[M145 FAIL] $port original ELF not executed: $elf" >&2; exit 1;
      }
    done
    ab_count=$(grep -Fc '[M144 HOST GET] /ab ' "$root/$host_log" || :)
    [ "$ab_count" -eq 4 ] || {
      echo "[M145 FAIL] $port ApacheBench host request count=$ab_count" >&2; exit 1;
    }
    grep -Fq '[M144 HOST GET] /haproxy ' "$root/$host_log" || {
      echo "[M145 FAIL] $port HAProxy has no real host backend request" >&2; exit 1;
    }
    ;;
esac
echo "[M145 QEMU PASS] $port/$cohort authentic Alpine services, MMIO NIC, DHCP, strict guest + host wire evidence"
