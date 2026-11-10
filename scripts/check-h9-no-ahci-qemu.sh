#!/bin/sh
# Same H9 ISO but NO SATA HDD. Q35 still has its built-in AHCI and ISO ATAPI;
# its optical drive may never satisfy the required readonly SATA ext4 root.
set -eu

out=build/h9-qemu-only
image="$out/naseed.iso"
serial_log="$out/h9-no-sata-uefi-serial.log"
clean="$out/h9-no-sata-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_H9_AHCI:-0}" = 0 ] || {
    echo '[H9 NEGATIVE FAIL] ambient SATA/VirtIO disk was supplied' >&2
    exit 2
}
for var in NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME \
           NAS_QEMU_H6_TWO_NVME NAS_QEMU_H7_NVME NAS_QEMU_H8_NVME; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H9 NEGATIVE FAIL] ambient NVMe profile $var" >&2
        exit 2
    }
done
rc=0
timeout 45s sh scripts/run.sh "$image" > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H9 NEGATIVE FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -130 "$clean" >&2
    exit 1
fi
grep -Fq '[H9 AHCI FAIL] required QEMU SATA disk unavailable' "$clean" || {
    echo '[H9 NEGATIVE FAIL] missing explicit real no-HDD rejection' >&2
    tail -130 "$clean" >&2
    exit 1
}
if grep -Eq 'H9 AHCI ROOT MOUNTED|H9_ORIGINAL_ALPINE_DYNAMIC_ELFS_SATA_PASS|H9_ALPINE_(LUA54|JQ|SQLITE)_SATA_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/' "$clean"; then
    echo '[H9 NEGATIVE FAIL] Alpine userland ran without a genuine SATA disk' >&2
    tail -130 "$clean" >&2
    exit 1
fi
echo '[H9 NO-SATA FAIL-CLOSED PASS] built-in Q35 AHCI and ATAPI ISO alone cannot supply Alpine SATA ext4 /; no CPIO/NVMe/VirtIO substitute'
