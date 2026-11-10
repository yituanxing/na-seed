#!/bin/sh
# H8: signature-verified, original Alpine musl BusyBox/Lua/jq/SQLite on an
# isolated, fixed-size, readonly QEMU NVMe ext4 ROOT.  The Limine rescue CPIO
# deliberately has NO Alpine executable, musl interpreter or guest workload.
set -eu

[ "$#" -le 1 ] || {
    echo 'usage: sh scripts/stage-h8-alpine-nvme.sh [build/h8-qemu-only]' >&2
    exit 2
}
out=${1:-build/h8-qemu-only}
# This is an isolated disposable QEMU fixture, never a physical device or a
# user-chosen disk image. Keep paths fixed and bounded, even for CI callers.
[ "$out" = build/h8-qemu-only ] || {
    echo '[H8 HOST FAIL] refusing non-fixture staging directory' >&2
    exit 2
}
image="$out/h8-alpine.ext4"
before="$out/h8-media-before.sha256"
root="build/external/m39-packages/amd64/rootfs"
tree="$out/fixture/disk-tree"
verify="$out/fixture/disk-verify"
rescue_root="$out/fixture/rescue-root"
rescue="$out/rescue.cpio"

for tool in docker sudo curl tar readelf sha256sum mke2fs debugfs \
            truncate cpio cmp grep find tr readlink; do
    command -v "$tool" >/dev/null || {
        echo "[H8 HOST FAIL] missing required $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[H8 HOST FAIL] output symlink refused: $path" >&2
        exit 1
    }
done

# The existing Alpine owner verifies the pinned official 3.23.7 minirootfs
# SHA256, imports Alpine release signing keys, and installs every APK with
# signature enforcement (never --allow-untrusted or host-built substitutes).
# Check the disk before doing any possible source-package staging work: an
# unknown or modified pre-existing namespace is an explicit stop condition.
if [ -e "$image" ]; then
    [ -f "$image" ] && [ -s "$before" ] &&
    (cd "$out" && sha256sum -c h8-media-before.sha256 >/dev/null) && {
        echo '[H8 HOST FIXTURE] authenticated existing readonly image'
    } || {
        echo '[H8 HOST FAIL] pre-existing unknown/modified NVMe image; refusing overwrite' >&2
        exit 1
    }
fi
NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh amd64
test -s "$root/bin/busybox"
test -s "$root/lib/ld-musl-x86_64.so.1"
test -s "$root/usr/bin/lua5.4"
test -s "$root/usr/bin/jq"
test -s "$root/usr/bin/sqlite3"
for elf in "$root/bin/busybox" "$root/usr/bin/lua5.4" \
           "$root/usr/bin/jq" "$root/usr/bin/sqlite3"; do
    readelf -h "$elf" | grep -Fq 'Advanced Micro Devices X86-64' || {
        echo "[H8 HOST FAIL] wrong ELF machine: $elf" >&2
        exit 1
    }
    readelf -h "$elf" | grep -Eq 'Type:[[:space:]]+DYN' || {
        echo "[H8 HOST FAIL] expected original PIE ELF: $elf" >&2
        exit 1
    }
done
readelf -l "$root/bin/busybox" | grep -Fq '/lib/ld-musl-x86_64.so.1' || {
    echo '[H8 HOST FAIL] Alpine BusyBox missing original musl PT_INTERP' >&2
    exit 1
}
readelf -h "$root/lib/ld-musl-x86_64.so.1" |
    grep -Fq 'Advanced Micro Devices X86-64' || {
    echo '[H8 HOST FAIL] wrong/missing original musl interpreter' >&2
    exit 1
}

if [ -e "$tree" ] || [ -L "$tree" ]; then
    # Re-running the same stage must never erase an unknown tree. Reuse only
    # a tree whose source ELF bytes AND workload exactly match this recipe.
    [ ! -L "$tree" ] && [ -d "$tree" ] &&
    cmp "$root/bin/busybox" "$tree/bin/busybox" >/dev/null &&
    cmp "$root/lib/ld-musl-x86_64.so.1" \
        "$tree/lib/ld-musl-x86_64.so.1" >/dev/null &&
    cmp "$root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" >/dev/null &&
    cmp "$root/usr/bin/jq" "$tree/usr/bin/jq" >/dev/null &&
    cmp "$root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" >/dev/null &&
    cmp scripts/h8-guest-workload.sh "$tree/root/h8-workload.sh" >/dev/null &&
    test "$(sed -n '1p' "$tree/root/h8-disk-only.txt")" = \
        H8_NVME_SIGNED_ALPINE_DISK_ONLY_20261110 || {
        echo '[H8 HOST FAIL] pre-existing staging tree differs; refusing overwrite' >&2
        exit 1
    }
else
    mkdir -p "$tree"
    cp -a "$root/." "$tree/"
    mkdir -p "$tree/root"
    cp scripts/h8-guest-workload.sh "$tree/root/h8-workload.sh"
    chmod 755 "$tree/root/h8-workload.sh"
    printf '%s\n' 'H8_NVME_SIGNED_ALPINE_DISK_ONLY_20261110' \
        > "$tree/root/h8-disk-only.txt"
    cp "$tree/root/h8-disk-only.txt" "$tree/root/h8-copy.txt"
fi
mkdir -p "$verify" "$rescue_root/etc"

# The CPIO is a valid archive that bootfs can index.  H8's real PID1 and
# interpreter MUST come from NVMe `/`, not a rescue BusyBox fallback.
printf '%s\n' 'NaSeed H8 rescue metadata only (no executable ELF)' \
    > "$rescue_root/etc/h8-rescue-only"
(
    cd "$rescue_root"
    find . -print | cpio -o -H newc --quiet --owner=0:0
) > "$rescue.tmp"
mv "$rescue.tmp" "$rescue"
if cpio -it --quiet < "$rescue" |
   grep -Eq '(^|/)(busybox|ld-musl-x86_64\.so\.1|lua5\.4|jq|sqlite3|h8-workload\.sh|h8-disk-only\.txt)$'; then
    echo '[H8 HOST FAIL] original Alpine program/data leaked into rescue' >&2
    exit 1
fi

if [ ! -e "$image" ]; then
    truncate -s 128M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-000000000008 \
        -d "$tree" "$image"
    (cd "$out" && sha256sum h8-alpine.ext4 > h8-media-before.sha256)
fi

# Independent readback from actual ext4 inodes: all FOUR binaries, workload
# script, and disk-only bytes must match the signed staged originals.
# Alpine's musl interpreter is often a relative symlink to its libc ELF.
# Resolve that link inside the staged guest root and verify its actual data
# inode on disk; never chase arbitrary absolute symlinks on the HOST.
loader_guest=lib/ld-musl-x86_64.so.1
if [ -L "$tree/$loader_guest" ]; then
    loader_link=$(readlink "$tree/$loader_guest")
    case "$loader_link" in
        /*) loader_guest=${loader_link#/} ;;
        ../*|./*|*/*)
            echo '[H8 HOST FAIL] unexpected musl interpreter symlink layout' >&2
            exit 1 ;;
        *) loader_guest="lib/$loader_link" ;;
    esac
fi
[ -f "$tree/$loader_guest" ] || {
    echo '[H8 HOST FAIL] musl interpreter target missing from ext4 tree' >&2
    exit 1
}
for guest_path in bin/busybox usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3 \
                  "$loader_guest" root/h8-workload.sh root/h8-disk-only.txt; do
    dump_name=$(printf '%s' "$guest_path" | tr / _)
    debugfs -R "dump /$guest_path $verify/$dump_name" "$image" \
        > /dev/null 2>&1 || {
            echo "[H8 HOST FAIL] cannot read ext4 /$guest_path" >&2
            exit 1
        }
    cmp "$tree/$guest_path" "$verify/$dump_name" || {
        echo "[H8 HOST FAIL] ext4 readback mismatch: /$guest_path" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h8-media-before.sha256)
sha256sum "$root/bin/busybox" "$tree/bin/busybox" \
    "$root/lib/ld-musl-x86_64.so.1" "$tree/lib/ld-musl-x86_64.so.1" \
    "$root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" \
    "$root/usr/bin/jq" "$tree/usr/bin/jq" \
    "$root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" \
    > "$out/h8-original-alpine-elfs.sha256"
echo '[H8 HOST FIXTURE PASS] 128MiB isolated readonly NVMe ext4 root; signed original dynamic Alpine BusyBox/musl/Lua/jq/SQLite; disk-only workload; no ELF in rescue'
