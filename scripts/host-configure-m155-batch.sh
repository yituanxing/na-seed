#!/bin/sh
# HOST-ONLY authentic filesystem images. They are packed as gzip snapshots:
# CPIO never carries 320 MiB of mostly-zero XFS data, and the guest inflates
# one bounded shared fixture per format before running original Alpine ELFs.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$root" in build/external/m155-batch/*/rootfs) ;; *)
    echo '[M155 HOST FIXTURE FAIL] unexpected staging destination' >&2
    exit 2 ;;
esac
for bin in mkfs.btrfs mkfs.xfs mkfs.f2fs mkntfs ntfscp mkfs.erofs parted gzip; do
    command -v "$bin" >/dev/null 2>&1 || {
        echo "[M155 HOST FIXTURE FAIL] missing $bin" >&2; exit 1;
    }
done
mkdir -p "$root/tests/m155-seed"
printf '%s\n' 'NASEED_M155_EVIDENCE original Alpine filesystem payload' \
    'Byte-exact image extraction and NTFS stream readback' \
    > "$root/tests/m155-seed/evidence.txt"
cp "$root/tests/m155-seed/evidence.txt" "$root/tests/m155-input.txt"

image="$root/tests/m155-btrfs.img"
truncate -s 64M "$image"
mkfs.btrfs -f -M -d single -m single -L NASEED155 "$image" \
    > "$root/tests/m155-btrfs-host.log" 2>&1

image="$root/tests/m155-xfs.img"
truncate -s 320M "$image"
mkfs.xfs -f -L NASEED155 "$image" \
    > "$root/tests/m155-xfs-host.log" 2>&1

image="$root/tests/m155-f2fs.img"
truncate -s 64M "$image"
mkfs.f2fs -f -l NASEED155 "$image" \
    > "$root/tests/m155-f2fs-host.log" 2>&1

image="$root/tests/m155-ntfs.img"
truncate -s 32M "$image"
mkntfs -F -Q -L NASEED155 "$image" \
    > "$root/tests/m155-ntfs-host.log" 2>&1
ntfscp "$image" "$root/tests/m155-input.txt" /evidence.txt \
    >> "$root/tests/m155-ntfs-host.log" 2>&1

image="$root/tests/m155-gpt.img"
truncate -s 16M "$image"
parted -s "$image" mklabel gpt mkpart primary 1MiB 9MiB \
    > "$root/tests/m155-gpt-host.log" 2>&1

# EROFS is tiny and stays raw. It contains actual host-authored file data.
mkfs.erofs -L NASEED155 -T 0 "$root/tests/m155-erofs.img" \
    "$root/tests/m155-seed" \
    > "$root/tests/m155-erofs-host.log" 2>&1

for kind in btrfs xfs f2fs ntfs gpt; do
    image="$root/tests/m155-$kind.img"
    test -s "$image" || exit 1
    gzip -1 -n "$image"
    gzip -t "$image.gz"
done
test -s "$root/tests/m155-erofs.img"
test "$(wc -c < "$root/tests/m155-input.txt")" -ge 70
# This upper bound catches accidentally embedding an uncompressed 320 MiB
# image in initramfs, which would duplicate it while the kernel boots.
packed=$(du -ck "$root"/tests/m155-*.img.gz | tail -1 | awk '{print $1}')
[ "$packed" -lt 16000 ] || {
    echo "[M155 HOST FIXTURE FAIL] compressed fixture ${packed} KiB" >&2
    exit 1
}
echo "[M155 HOST FIXTURE PASS] $port Btrfs/XFS/F2FS/NTFS/GPT gzip images + EROFS payload; ${packed} KiB"
