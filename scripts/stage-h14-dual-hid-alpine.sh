#!/bin/sh
# H14 extends H8's verified signed original Alpine NVMe root with *only*
# host-verified signed Alpine APK content (including the original evtest ELF).
# The UEFI rescue CPIO contains no executable. The HID input source is a
# separate real QEMU USB mouse, not an image or pre-recorded event stream.
set -eu

out=build/h14-qemu-only
image="$out/h14-alpine.ext4"
before="$out/h14-media-before.sha256"
originals="$out/h14-original-alpine-elfs.sha256"
tree="$out/fixture/disk-tree"
verify="$out/fixture/disk-verify"
rescue_root="$out/fixture/rescue-root"
rescue="$out/rescue.cpio"
h8_root=build/external/m39-packages/amd64/rootfs
signed_root=build/external/m166-batch/amd64/rootfs

for tool in docker sudo curl tar readelf sha256sum mke2fs debugfs \
            truncate cpio cmp grep find tr stat chmod; do
    command -v "$tool" >/dev/null || {
        echo "[H14 HOST FAIL] missing required $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$originals" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[H14 HOST FAIL] symlink output refused: $path" >&2
        exit 2
    }
done

if [ -e "$image" ]; then
    [ -f "$image" ] && test -s "$before" &&
    test -s "$originals" && test -s "$rescue" &&
    (cd "$out" && sha256sum -c h14-media-before.sha256 >/dev/null) || {
        echo '[H14 HOST FAIL] unknown, modified or incomplete existing dual HID fixture' >&2
        exit 1
    }
else
    [ ! -e "$tree" ] && [ ! -L "$tree" ] || {
        echo '[H14 HOST FAIL] foreign/stale staging tree; refusing overwrite' >&2
        exit 1
    }
    # H8 verifies the Alpine 3.23 release image/digests, APK signing keys,
    # original musl interpreter, native BusyBox/Lua/jq/SQLite and rootfs.
    sh scripts/stage-h8-alpine-nvme.sh
    # M166 is the already-established authorized signed Alpine APK stage for
    # evtest/libevdev. No guest APK invocation or unsigned C substitute.
    sh scripts/stage-m166-prebuilt.sh amd64
    for guest_path in bin/busybox lib/ld-musl-x86_64.so.1 \
                      usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3; do
        test -s "$h8_root/$guest_path" && test -s "$signed_root/$guest_path" &&
        cmp "$h8_root/$guest_path" "$signed_root/$guest_path" || {
            echo "[H14 HOST FAIL] H8 baseline original changed: $guest_path" >&2
            exit 1
        }
    done
    grep -Fxq 'P:evtest' "$signed_root/lib/apk/db/installed" || {
        echo '[H14 HOST FAIL] signed Alpine evtest APK missing' >&2
        exit 1
    }
    test -x "$signed_root/usr/bin/evtest" &&
    readelf -h "$signed_root/usr/bin/evtest" |
        grep -Fq 'Advanced Micro Devices X86-64' || {
        echo '[H14 HOST FAIL] original evtest is missing/non-x86 ELF' >&2
        exit 1
    }
    # The ext4 root remains read-only. The early bootfs /tmp mountpoint is
    # required for the volatile log of the genuine, background Alpine evtest;
    # no executable or event recording is staged in this rescue archive.
    mkdir -p "$tree" "$verify" "$rescue_root/etc" "$rescue_root/tmp"
    chmod 1777 "$rescue_root/tmp"
    cp -a "$signed_root/." "$tree/"
    mkdir -p "$tree/root"
    cp scripts/h14-guest-workload.sh "$tree/root/h14-workload.sh"
    chmod 755 "$tree/root/h14-workload.sh"
    printf '%s\n' 'H14_DUAL_SIGNED_ALPINE_DISK_ONLY_20261110' \
        > "$tree/root/h14-disk-only.txt"
    cp "$tree/root/h14-disk-only.txt" "$tree/root/h14-copy.txt"

    printf '%s\n' 'NaSeed H14 rescue metadata only; no original Alpine ELF' \
        > "$rescue_root/etc/h14-rescue-only"
    (
        cd "$rescue_root"
        find . -print | cpio -o -H newc --quiet --owner=0:0
    ) > "$rescue.tmp"
    mv "$rescue.tmp" "$rescue"

    # The disk may contain extra original signed M166 packages, but none
    # appears in CPIO or can satisfy evdev without genuine USB mouse events.
    truncate -s 256M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-000000000014 \
        -d "$tree" "$image"
    chmod 0444 "$image"
    (cd "$out" && sha256sum h14-alpine.ext4 > h14-media-before.sha256)
    sha256sum "$h8_root/bin/busybox" "$tree/bin/busybox" \
        "$h8_root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" \
        "$h8_root/usr/bin/jq" "$tree/usr/bin/jq" \
        "$h8_root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" \
        "$signed_root/usr/bin/evtest" "$tree/usr/bin/evtest" \
        > "$originals"
fi

if cpio -it --quiet < "$rescue" |
   grep -Eq '(^|/)(busybox|ld-musl-x86_64\.so\.1|lua5\.4|jq|sqlite3|evtest|h14-workload\.sh|h14-disk-only\.txt)$'; then
    echo '[H14 HOST FAIL] original program/evtest data leaked into rescue CPIO' >&2
    exit 1
fi
cpio -itv --quiet < "$rescue" |
    grep -Eq '^drwxrwxrwt[[:space:]]+.*[[:space:]]\.?/?tmp$' || {
    echo '[H14 HOST FAIL] ELF-free rescue lacks writable sticky RAM /tmp mountpoint' >&2
    exit 1
}
(cd "$out" && sha256sum -c h14-media-before.sha256)
[ "$(stat -c '%a' "$image")" = 444 ] || {
    echo '[H14 HOST FAIL] NVMe media not protected readonly' >&2
    exit 1
}
sha256sum -c "$originals"

mkdir -p "$verify"
for guest_path in bin/busybox usr/bin/lua5.4 usr/bin/jq \
                  usr/bin/sqlite3 usr/bin/evtest \
                  root/h14-workload.sh root/h14-disk-only.txt; do
    dump_name=$(printf '%s' "$guest_path" | tr / _)
    debugfs -R "dump /$guest_path $verify/$dump_name" "$image" \
        >/dev/null 2>&1 || {
            echo "[H14 HOST FAIL] cannot read NVMe ext4 /$guest_path" >&2
            exit 1
        }
    cmp "$tree/$guest_path" "$verify/$dump_name" || {
        echo "[H14 HOST FAIL] NVMe ext4 inode mismatch: /$guest_path" >&2
        exit 1
    }
done
cmp scripts/h14-guest-workload.sh "$verify/root_h14-workload.sh" || {
    echo '[H14 HOST FAIL] stale guest mouse evtest workload on disk' >&2
    exit 1
}
echo '[H14 HOST FIXTURE PASS] original signed Alpine H8 userland + signed Alpine evtest APK on immutable NVMe ext4; ELF-free rescue, exact inode/SHA evidence'
