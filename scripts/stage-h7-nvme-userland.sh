#!/bin/sh
# H7: disposable ext4 namespace with the ORIGINAL checksum-pinned Debian
# BusyBox ELF.  Only this checked-in recipe may populate H7's QEMU disk.
# Never accept an external device, path, pre-existing filesystem or volume.
set -eu

out=build/h7-qemu-only
image="$out/h7-ext4.img"
source="$out/fixture/debian-busybox"
bash_source="$out/fixture/debian/userspace/debian-bash"
disk_root="$out/fixture/disk-tree"
rescue="$out/fixture/rescue"
for tool in sha256sum readelf mke2fs debugfs truncate cpio cmp dpkg-deb curl; do
    command -v "$tool" >/dev/null || {
        echo "[H7 HOST FAIL] missing required $tool" >&2
        exit 1
    }
done

mkdir -p "$out/fixture"
sh scripts/fetch-busybox-fixture.sh amd64 "$source"
sh scripts/fetch-debian.sh bash "$out/fixture/debian"
test -s "$source" && test -s "$source.fixture"
test -s "$bash_source"
readelf -h "$source" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$source" | grep -Fq 'EXEC (Executable file)'
if readelf -l "$source" | grep -Fq INTERP; then
    echo '[H7 HOST FAIL] Debian BusyBox unexpectedly requires ELF interpreter' >&2
    exit 1
fi
readelf -h "$bash_source" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$bash_source" | grep -Fq 'EXEC (Executable file)'
if readelf -l "$bash_source" | grep -Fq INTERP; then
    echo '[H7 HOST FAIL] Debian static Bash unexpectedly requires interpreter' >&2
    exit 1
fi

# Generate both root trees from one independently fetched ELF, but leave the
# NVMe data fixture completely absent from the CPIO rescue image.  A CPIO-only
# boot can therefore NEVER produce the H7 file-content acceptance output.
mkdir -p "$disk_root/bin" "$disk_root/sandbox" "$disk_root/etc"
cp "$source" "$disk_root/bin/busybox"
cp "$bash_source" "$disk_root/bin/bash"
chmod 755 "$disk_root/bin/busybox" "$disk_root/bin/bash"
printf '%s\n' 'H7_NVME_EXT4_DEBIAN_BUSYBOX_DISK_ONLY_20261110' \
    > "$disk_root/sandbox/host-seed.txt"
cp "$disk_root/sandbox/host-seed.txt" \
    "$disk_root/sandbox/host-copy.txt"
printf '%s\n' 'NaSeed H7 read-only NVMe ext4 application source' \
    > "$disk_root/etc/h7-source"
chmod 644 "$disk_root/sandbox/host-seed.txt" \
    "$disk_root/sandbox/host-copy.txt" "$disk_root/etc/h7-source"

sh scripts/stage-busybox-rootfs.sh "$source" "$rescue"
cp scripts/h7-guest-workload.sh "$rescue/rootfs/h7-guest-workload.sh"
chmod 755 "$rescue/rootfs/h7-guest-workload.sh"
(
    cd "$rescue/rootfs"
    find . -print | cpio -o -H newc --quiet --owner=0:0
) > "$rescue/rootfs.cpio.tmp"
mv "$rescue/rootfs.cpio.tmp" "$rescue/rootfs.cpio"
if cpio -it --quiet < "$rescue/rootfs.cpio" |
   grep -Eq '(^|/)(sandbox|host-seed\.txt|host-copy\.txt|h7-source)(/|$)'; then
    echo '[H7 HOST FAIL] NVMe-only data leaked into rescue CPIO' >&2
    exit 1
fi

# The QEMU namespace is neither a real device nor an arbitrary input image.
# This fresh, bounded root image is copied onto a QEMU NVMe namespace with
# readonly=on; the guest's BlockDevice adapter must independently reject
# writes with -EROFS.  Disable unsupported ext4 features as in prior gates.
if [ -e "$image" ] || [ -L "$image" ]; then
    # A generation script must not silently overwrite an unknown existing
    # image (or follow a malicious image symlink). Reuse only media proven
    # byte-identical to this profile's preceding full image checksum.
    [ ! -L "$image" ] && [ -f "$image" ] &&
    [ -s "$out/h7-media-before.sha256" ] &&
    sha256sum -c "$out/h7-media-before.sha256" >/dev/null || {
        echo '[H7 HOST FAIL] unknown/modified pre-existing H7 disk; refusing overwrite' >&2
        exit 1
    }
    echo '[H7 HOST FIXTURE] reused authenticated fixed H7 readonly image'
else
    truncate -s 32M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-000000000007 \
        -d "$disk_root" "$image"
    sha256sum "$image" > "$out/h7-media-before.sha256"
fi
# Independently re-read actual on-disk ext4 inodes on the host before QEMU,
# not merely copies in the source tree that never reached the filesystem.
disk_verify="$out/fixture/disk-verify"
mkdir -p "$disk_verify"
debugfs -R "dump /bin/busybox $disk_verify/busybox" "$image" \
    > /dev/null 2>&1
debugfs -R "dump /bin/bash $disk_verify/bash" "$image" \
    > /dev/null 2>&1
debugfs -R "dump /sandbox/host-seed.txt $disk_verify/host-seed.txt" \
    "$image" > /dev/null 2>&1
cmp "$source" "$disk_verify/busybox"
cmp "$bash_source" "$disk_verify/bash"
cmp "$disk_root/sandbox/host-seed.txt" "$disk_verify/host-seed.txt"
sha256sum "$source" "$disk_root/bin/busybox" "$bash_source" \
    "$disk_root/bin/bash" > "$out/h7-elfs.sha256"
echo '[H7 HOST FIXTURE PASS] fixed disposable 32MiB NVMe ext4 + pinned original Debian BusyBox/Bash static ELFs; rescue has no NVMe data'
