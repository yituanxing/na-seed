#!/bin/sh
# HOST-ONLY functional fixtures. No guest mkfs, package install or compiler.
# The official sleuthkit ELFs inspect an actual ext2 filesystem and DOS MBR
# rather than passing a meaningless --help or fabricated result string.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m150-batch/*/rootfs) ;; *)
    echo '[M150 HOST FIXTURE FAIL] unexpected signed staging root' >&2; exit 2 ;;
esac
mkdir -p "$root/tests"
command -v mke2fs >/dev/null 2>&1 || {
    echo '[M150 HOST FIXTURE FAIL] mke2fs required (e2fsprogs)' >&2; exit 1;
}
seed="$root/tests/m150-seed"
mkdir -p "$seed"
printf '%s\n' 'NASEED_M150_EVIDENCE original Alpine ELF content proof' \
    > "$seed/evidence.txt"
printf '%s\n' 'NaSeed signed original software archive roundtrip' \
    'Sleuthkit inspects a real ext2 filesystem and MBR partition table' \
    > "$root/tests/m150-input.txt"
fs="$root/tests/m150-ext2.img"
disk="$root/tests/m150-mbr.img"
truncate -s 8388608 "$fs"
mke2fs -q -F -t ext2 -b 1024 -d "$seed" "$fs" >/dev/null 2>&1
truncate -s 17825792 "$disk"  # 1MiB before 8MiB partition plus slack
# One real Linux (0x83) primary partition at LBA 2048, 16384 sectors long.
# Byte-exact DOS partition entry at offset 446; signature 55 aa at 510.
printf '\000\000\002\000\203\377\377\377\000\010\000\000\000\100\000\000' |
    dd of="$disk" bs=1 seek=446 conv=notrunc status=none
printf '\125\252' | dd of="$disk" bs=1 seek=510 conv=notrunc status=none
dd if="$fs" of="$disk" bs=512 seek=2048 conv=notrunc status=none
test "$(wc -c < "$fs")" -eq 8388608
test "$(wc -c < "$disk")" -eq 17825792
echo "[M150 HOST FIXTURE PASS] $port ext2 8MiB + DOS MBR, signed ELFs unchanged"
