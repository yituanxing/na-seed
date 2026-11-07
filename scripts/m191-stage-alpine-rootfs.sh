#!/bin/sh
# Stage the unmodified Alpine 3.23.7 native minirootfs and signed Alpine
# base/OpenRC packages on the HOST. Output is an independent physical ext4
# root disk: the M174 CPIO and M178-M190 16MiB data-disk oracles stay intact.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: m191-stage-alpine-rootfs.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
for tool in docker sudo tar readelf mke2fs debugfs e2fsck sha256sum; do
    command -v "$tool" >/dev/null || {
        echo "[M191 HOST FAIL] missing $tool" >&2
        exit 1
    }
done

# The existing Alpine build owner already downloads and checks the exact
# official release tarball, imports the target-ISA signing keys, and uses
# signature-verifying apk --arch --root. M191 is the only full-tar profile.
NAS_ALPINE_PROFILE=m191-rootfs sh rootfs/alpine/build.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$work/rootfs"
test -d "$root/etc/apk" && test -s "$root/etc/alpine-release"
[ "$(stat -c '%u:%g' "$root")" = '0:0' ] || {
    echo '[M191 HOST FAIL] official ext4 root inode must belong to UID/GID 0' >&2
    exit 1
}

# Guest root may itself have absolute /bin, /sbin or /var/run symlinks.
# Configure through the guest-path resolver; never let sudo follow such a
# link into the Linux HOST filesystem.
sudo python3 scripts/m191-alpine-root-disk.py configure "$port" "$root"

sudo python3 scripts/m191-alpine-root-disk.py check-tree "$port" "$root"
disk="$work/alpine-root.img"
# `prepare` already runs physical-disk verification before atomic publish;
# do not extract/fsck the same 254MiB partition a redundant second time.
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$root" "$disk"
# The actual QEMU runner is deliberately unprivileged and must be able to
# write this physical disk. Restore only output ownership, not APK files.
sudo chown "$(id -u):$(id -g)" "$disk"
echo "[M191 HOST ROOTFS PASS] $port signed original Alpine 3.23.7 userspace, ext4 disk and independent host readback"
