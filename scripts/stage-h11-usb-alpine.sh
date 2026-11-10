#!/bin/sh
# Independent original signed Alpine root, with H11-only marker and workload.
# No executable is present in the UEFI rescue CPIO; Alpine must be loaded by
# actual xHCI -> USB Mass Storage -> readonly SCSI READ(10) -> ext4 -> execve.
set -eu

out=build/h11-qemu-only
image="$out/h11-alpine.ext4"
before="$out/h11-media-before.sha256"
originals="$out/h11-original-alpine-elfs.sha256"
tree="$out/fixture/disk-tree"
verify="$out/fixture/disk-verify"
rescue_root="$out/fixture/rescue-root"
rescue="$out/rescue.cpio"
source_root=build/external/m39-packages/amd64/rootfs

for tool in docker sudo curl tar readelf sha256sum mke2fs debugfs \
            truncate cpio cmp grep find tr stat chmod; do
    command -v "$tool" >/dev/null || {
        echo "[H11 HOST FAIL] missing required $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$originals" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[H11 HOST FAIL] symlink output refused: $path" >&2
        exit 2
    }
done

if [ -e "$image" ]; then
    [ -f "$image" ] && test -s "$before" &&
    test -s "$originals" && test -s "$rescue" &&
    (cd "$out" && sha256sum -c h11-media-before.sha256 >/dev/null) || {
        echo '[H11 HOST FAIL] unknown, modified or incomplete existing USB fixture' >&2
        exit 1
    }
else
    [ ! -e "$tree" ] && [ ! -L "$tree" ] || {
        echo '[H11 HOST FAIL] foreign/stale staging tree; refusing overwrite' >&2
        exit 1
    }
    # Existing H8 builder verifies original Alpine minirootfs, release keys,
    # signed APKs and exact original ELF bytes, not self-built replacements.
    sh scripts/stage-h8-alpine-nvme.sh
    for path in bin/busybox lib/ld-musl-x86_64.so.1 \
                usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3; do
        test -s "$source_root/$path" || {
            echo "[H11 HOST FAIL] missing original Alpine $path" >&2
            exit 1
        }
    done
    mkdir -p "$tree" "$verify" "$rescue_root/etc"
    cp -a "$source_root/." "$tree/"
    mkdir -p "$tree/root"
    cp scripts/h11-guest-workload.sh "$tree/root/h11-workload.sh"
    chmod 755 "$tree/root/h11-workload.sh"
    printf '%s\n' 'H11_USB_SIGNED_ALPINE_DISK_ONLY_20261110' \
        > "$tree/root/h11-disk-only.txt"
    cp "$tree/root/h11-disk-only.txt" "$tree/root/h11-copy.txt"

    printf '%s\n' 'NaSeed H11 rescue metadata only; no executable ELF' \
        > "$rescue_root/etc/h11-rescue-only"
    (
        cd "$rescue_root"
        find . -print | cpio -o -H newc --quiet --owner=0:0
    ) > "$rescue.tmp"
    mv "$rescue.tmp" "$rescue"

    truncate -s 128M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-000000000011 \
        -d "$tree" "$image"
    # USB Mass Storage QEMU frontend may issue its own writes. It sees only a
    # disposable snapshot while raw media remains immutable, chmod 0444.
    # NaSeed's USB BlockDevice must never issue a write-capable SCSI command.
    chmod 0444 "$image"
    (cd "$out" && sha256sum h11-alpine.ext4 > h11-media-before.sha256)
    sha256sum "$source_root/bin/busybox" "$tree/bin/busybox" \
        "$source_root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" \
        "$source_root/usr/bin/jq" "$tree/usr/bin/jq" \
        "$source_root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" \
        > "$originals"
fi

if cpio -it --quiet < "$rescue" |
   grep -Eq '(^|/)(busybox|ld-musl-x86_64\.so\.1|lua5\.4|jq|sqlite3|h11-workload\.sh|h11-disk-only\.txt)$'; then
    echo '[H11 HOST FAIL] Alpine programs leaked into rescue CPIO' >&2
    exit 1
fi
(cd "$out" && sha256sum -c h11-media-before.sha256)
[ "$(stat -c '%a' "$image")" = 444 ] || {
    echo '[H11 HOST FAIL] USB raw backing image is not chmod 0444' >&2
    exit 1
}
sha256sum -c "$originals"

# Read back every executable plus the H11-only marker from the actual ext4
# inode table. Existence in a host staging folder alone is not evidence.
mkdir -p "$verify"
for guest_path in bin/busybox usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3 \
                  root/h11-workload.sh root/h11-disk-only.txt; do
    dump_name=$(printf '%s' "$guest_path" | tr / _)
    debugfs -R "dump /$guest_path $verify/$dump_name" "$image" \
        >/dev/null 2>&1 || {
            echo "[H11 HOST FAIL] cannot read USB ext4 /$guest_path" >&2
            exit 1
        }
    cmp "$tree/$guest_path" "$verify/$dump_name" || {
        echo "[H11 HOST FAIL] USB ext4 inode readback mismatch: /$guest_path" >&2
        exit 1
    }
done
cmp scripts/h11-guest-workload.sh "$verify/root_h11-workload.sh" || {
    echo '[H11 HOST FAIL] USB image contains stale workload' >&2
    exit 1
}
echo '[H11 HOST FIXTURE PASS] immutable signed 128MiB Alpine ext4 USB media; ELF-free rescue; verified source and ext4 inode readback'
