#!/bin/sh
# H9: stage the SAME genuine Alpine signed APK application set accepted in
# H8, now on a separate, fixed, readonly QEMU ICH9 AHCI SATA ext4 root.
# No physical SATA path or caller-selected disk image is ever accepted.
set -eu

[ "$#" -le 1 ] || {
    echo 'usage: sh scripts/stage-h9-ahci-alpine.sh [build/h9-qemu-only]' >&2
    exit 2
}
out=${1:-build/h9-qemu-only}
[ "$out" = build/h9-qemu-only ] || {
    echo '[H9 HOST FAIL] refusing non-fixture staging directory' >&2
    exit 2
}
image="$out/h9-alpine.ext4"
before="$out/h9-media-before.sha256"
originals="$out/h9-original-alpine-elfs.sha256"
tree="$out/fixture/disk-tree"
verify="$out/fixture/disk-verify"
rescue_root="$out/fixture/rescue-root"
rescue="$out/rescue.cpio"
source_root=build/external/m39-packages/amd64/rootfs

for tool in docker sudo curl tar readelf sha256sum mke2fs debugfs \
            truncate cpio cmp grep find tr; do
    command -v "$tool" >/dev/null || {
        echo "[H9 HOST FAIL] missing required $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$originals" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[H9 HOST FAIL] symlink output refused: $path" >&2
        exit 2
    }
done

# Idempotent only for the exact previously authenticated image. Its guest
# script and unique H9 marker are read back below even on a reuse attempt.
if [ -e "$image" ]; then
    [ -f "$image" ] && test -s "$before" &&
    test -s "$originals" && test -s "$rescue" &&
    (cd "$out" && sha256sum -c h9-media-before.sha256 >/dev/null) || {
        echo '[H9 HOST FAIL] unknown, modified or incomplete existing SATA fixture' >&2
        exit 1
    }
else
    [ ! -e "$tree" ] && [ ! -L "$tree" ] || {
        echo '[H9 HOST FAIL] foreign/stale staging tree; refusing overwrite' >&2
        exit 1
    }
    # This existing, historically verified H8 builder checks the pinned
    # official Alpine minirootfs digest, release keys, signed APKs, source
    # ELFs, and the complete original runtime tree. No local ELF substitute.
    sh scripts/stage-h8-alpine-nvme.sh
    test -s "$source_root/bin/busybox"
    test -s "$source_root/lib/ld-musl-x86_64.so.1"
    test -s "$source_root/usr/bin/lua5.4"
    test -s "$source_root/usr/bin/jq"
    test -s "$source_root/usr/bin/sqlite3"
    mkdir -p "$tree" "$verify" "$rescue_root/etc"
    cp -a "$source_root/." "$tree/"
    mkdir -p "$tree/root"
    cp scripts/h9-guest-workload.sh "$tree/root/h9-workload.sh"
    chmod 755 "$tree/root/h9-workload.sh"
    printf '%s\n' 'H9_SATA_SIGNED_ALPINE_DISK_ONLY_20261110' \
        > "$tree/root/h9-disk-only.txt"
    cp "$tree/root/h9-disk-only.txt" "$tree/root/h9-copy.txt"

    printf '%s\n' 'NaSeed H9 rescue metadata only; no executable ELF' \
        > "$rescue_root/etc/h9-rescue-only"
    (
        cd "$rescue_root"
        find . -print | cpio -o -H newc --quiet --owner=0:0
    ) > "$rescue.tmp"
    mv "$rescue.tmp" "$rescue"

    truncate -s 128M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-000000000009 \
        -d "$tree" "$image"
    (cd "$out" && sha256sum h9-alpine.ext4 > h9-media-before.sha256)
    sha256sum "$source_root/bin/busybox" "$tree/bin/busybox" \
        "$source_root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" \
        "$source_root/usr/bin/jq" "$tree/usr/bin/jq" \
        "$source_root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" \
        > "$originals"
fi

if cpio -it --quiet < "$rescue" |
   grep -Eq '(^|/)(busybox|ld-musl-x86_64\.so\.1|lua5\.4|jq|sqlite3|h9-workload\.sh|h9-disk-only\.txt)$'; then
    echo '[H9 HOST FAIL] original Alpine program/data leaked into rescue CPIO' >&2
    exit 1
fi
(cd "$out" && sha256sum -c h9-media-before.sha256)
sha256sum -c "$originals"

# Actual ext4 inode readback, not merely a host directory or filename proof.
mkdir -p "$verify"
for guest_path in bin/busybox usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3 \
                  root/h9-workload.sh root/h9-disk-only.txt; do
    dump_name=$(printf '%s' "$guest_path" | tr / _)
    debugfs -R "dump /$guest_path $verify/$dump_name" "$image" \
        >/dev/null 2>&1 || {
            echo "[H9 HOST FAIL] cannot read SATA ext4 /$guest_path" >&2
            exit 1
        }
    cmp "$tree/$guest_path" "$verify/$dump_name" || {
        echo "[H9 HOST FAIL] SATA ext4 readback mismatch: /$guest_path" >&2
        exit 1
    }
done
cmp scripts/h9-guest-workload.sh "$verify/root_h9-workload.sh" || {
    echo '[H9 HOST FAIL] SATA image contains stale H9 application workload' >&2
    exit 1
}
echo '[H9 HOST FIXTURE PASS] fixed 128MiB readonly AHCI SATA ext4 root; same signed original Alpine dynamic BusyBox/musl/Lua/jq/SQLite; disk-only workload, no ELF in rescue'
