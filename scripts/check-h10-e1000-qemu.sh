#!/bin/sh
# Real original Alpine BusyBox wget over actual PCI E1000 RX/TX, lwIP DHCP,
# TCP and QEMU usernet to an independent host-only random HTTP source.
set -eu
out=build/h10-qemu-only
image="$out/naseed.iso"
disk="$out/h10-alpine.ext4"
serial_log="$out/h10-qemu-uefi-serial.log"
clean="$out/h10-qemu-uefi-clean.log"
work=$out
for path in "$image" "$disk" "$out/h10-media-before.sha256" \
            "$out/h10-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H10 QEMU FAIL] missing/symlinked $path" >&2
        exit 2
    }
done
for var in NAS_VIRTIO_NET_USER NAS_VIRTIO_NET_SECOND_USER \
           NAS_QEMU_H8_NVME NAS_QEMU_H9_AHCI; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H10 QEMU FAIL] ambient extra device $var" >&2
        exit 2
    }
done
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] || {
    echo '[H10 QEMU FAIL] ambient VirtIO block image' >&2
    exit 2
}
(cd "$out" && sha256sum -c h10-media-before.sha256)
sha256sum -c "$out/h10-original-alpine-elfs.sha256"
. scripts/m223-http-host.sh
trap 'm223_cleanup' EXIT
m223_start_host first
source_body="$PWD/$out/m223-host-first/wget.source"
test -s "$source_body"
expected_sha=$(sha256sum "$source_body")
set -- $expected_sha
expected_sha=$1
expected_bytes=$(wc -c < "$source_body" | tr -d ' ')

rc=0
NAS_QEMU_H10_E1000=1 timeout 300s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    echo "[H10 QEMU FAIL] guest did not exit 0 via isa-debug-exit rc=1, observed $rc" >&2
    tail -220 "$clean" >&2
    exit 1
fi
for required in \
    '[H10 E1000 BAR QUALIFIED]' \
    '[H10 E1000 NETDEV ATTACHED] name=e10000' \
    '[H10 E1000 BOOT CONTINUE]' \
    '[LWIP DHCP BOUND] addr=10.0.2.' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    'H8_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS' \
    'H10_E1000_ORIGINAL_ALPINE_WGET_PASS' \
    '[H10 E1000 QUIESCED RELEASED]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H10 QEMU FAIL] missing actual guest evidence: $required" >&2
        tail -220 "$clean" >&2
        exit 1
    fi
done
grep -Fxq "H10_E1000_HTTP_SHA256=$expected_sha" "$clean" || {
    echo '[H10 QEMU FAIL] original guest wget SHA mismatches host-only random body' >&2
    tail -200 "$clean" >&2
    exit 1
}
host_count=$(grep -Ec "^M223_HOST_GET phase=first path=/wget status=200 bytes=$expected_bytes sha256=$expected_sha peer=" "$m223_log" || :)
[ "$host_count" -eq 1 ] || {
    echo "[H10 QEMU FAIL] host did not actually receive exactly one HTTP GET; count=$host_count" >&2
    tail -80 "$m223_log" >&2
    exit 1
}
test -s "$out/h10-wire.pcap" &&
test "$(wc -c < "$out/h10-wire.pcap")" -gt 100 || {
    echo '[H10 QEMU FAIL] no captured real E1000 Ethernet frames' >&2
    exit 1
}
if grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|H10_E1000_HTTP_TIMEOUT' "$clean" ||
   grep -Fq '[VIRTIO-NET]' "$clean"; then
    echo '[H10 QEMU FAIL] guest error or VirtIO-Net substitution' >&2
    tail -200 "$clean" >&2
    exit 1
fi
(cd "$out" && sha256sum -c h10-media-before.sha256)
sha256sum -c "$out/h10-original-alpine-elfs.sha256"
echo "[H10 REAL E1000 HTTP PASS] original Alpine BusyBox wget over PCI E1000 DMA/lwIP DHCP/TCP; host GET bytes=$expected_bytes sha256=$expected_sha; guest exit 0; immutable NVMe SHA"
tail -160 "$clean"
