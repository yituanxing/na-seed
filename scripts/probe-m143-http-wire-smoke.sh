#!/bin/sh
# One real x86 native QEMU boot, exact signed APK binaries, independent HTTP oracle.
set -eu
[ "$#" -eq 0 ] || exit 2
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m143-http-cases.tsv
sh scripts/stage-m143-prebuilt.sh amd64
root=build/external/m143-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m143-original-signed-elfs.sha256"
test -s "$root/rootfs/tests/m143-http-run.sh"
for elf in usr/bin/wget usr/bin/aria2c usr/sbin/lighttpd; do
    test -s "$root/rootfs/$elf"
    grep -Fxq "$elf" scripts/m143-original-elfs.txt
done
for elf in curl openssl; do test -s "$root/rootfs/usr/bin/$elf"; done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"
export NAS_VIRTIO_NET_PCAP="$root/virtio-m143-http.pcap"
python3 -u scripts/m143-http-server.py > "$root/host-m143-http.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5; do
    if grep -Fq '[M143 HOST HTTP READY]' "$root/host-m143-http.log"; then
        ready=true
        break
    fi
    sleep 1
done
[ "$ready" = true ] || { echo '[M143 FAIL] host server unavailable' >&2; exit 1; }
sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m143-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$clean"
for marker in M143_REAL_HTTP_WGET_DOWNLOAD_PASS \
              M143_REAL_HTTP_ARIA2_DOWNLOAD_PASS \
              M143_REAL_HTTP_CURL_DOWNLOAD_PASS \
              M143_REAL_OPENSSL_SHA256_PASS \
              M143_REAL_LIGHTTPD_LOOPBACK_PASS \
              M143_HTTP_COHORT_PASS; do
    grep -Fxq "$marker" "$clean" || {
        echo "[M143 FAIL] missing guest $marker" >&2; exit 1;
    }
done
for elf in /usr/bin/wget /usr/bin/aria2c /usr/sbin/lighttpd \
           /usr/bin/curl /usr/bin/openssl; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $elf" "$clean" || {
        echo "[M143 FAIL] original ELF did not run: $elf" >&2; exit 1;
    }
done
for entry in '/wget bytes=29' '/aria2 bytes=30' '/curl bytes=29'; do
    grep -Fq "[M143 HOST HTTP GET] $entry" "$root/host-m143-http.log" || {
        echo "[M143 FAIL] independent host did not receive $entry" >&2
        cat "$root/host-m143-http.log" >&2
        exit 1
    }
done
echo '[M143 HTTP QEMU PASS] three NEW signed Alpine ELFs, host wire GET/download and guest lighttpd loopback; curl/openssl regressions'
