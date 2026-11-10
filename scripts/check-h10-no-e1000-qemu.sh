#!/bin/sh
# Real QEMU no-NIC negative: retain the SAME signed Alpine NVMe boot image,
# suppress QEMU's implicit E1000e, and omit our explicit 82540EM function.
# Neither the default Q35 e1000e nor a stub NIC may satisfy H10.
set -eu
out=build/h10-qemu-only
image="$out/naseed.iso"
serial="$out/h10-no-e1000-uefi-serial.log"
clean="$out/h10-no-e1000-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial" ] || exit 2
[ "${NAS_QEMU_H10_E1000:-0}" = 0 ] &&
[ "${NAS_VIRTIO_NET_USER:-0}" = 0 ] || {
    echo '[H10 NEGATIVE FAIL] ambient NIC supplied' >&2
    exit 2
}
rc=0
NAS_QEMU_H10_NO_E1000=1 timeout 45s sh scripts/run.sh "$image" \
    > "$serial" 2>&1 || rc=$?
tr -d '\r' < "$serial" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H10 NEGATIVE FAIL] unexpected no-NIC QEMU rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq '[H10 E1000 FAIL] required QEMU E1000 missing; no Alpine network userland' "$clean" || {
    echo '[H10 NEGATIVE FAIL] missing explicit PCI E1000 absence rejection' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq 'H10_E1000_ORIGINAL_ALPINE_WGET_PASS|H10_E1000_HTTP_SHA256|H10 E1000 NETDEV ATTACHED|\[LWIP DHCP BOUND\]' "$clean"; then
    echo '[H10 NEGATIVE FAIL] network userland ran without the E1000' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H10 NO-E1000 FAIL-CLOSED PASS] same genuine signed Alpine NVMe root; no ambient Q35 E1000e; PCI E1000 absence blocks original wget'
