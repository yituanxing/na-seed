#!/bin/sh
# Shared exact H12–H15 signed Alpine fixture construction and validation.
# Profile controls paths and historic markers only; no weakened proof gates.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
case "$profile" in
  h12) prefix=H12; number=12;
    fixture='[H12 HOST FAIL] unknown, modified or incomplete existing HID fixture'
    disk_tag='H12_HID_SIGNED_ALPINE_DISK_ONLY_20261110'
    leak='[H12 HOST FAIL] original program/evtest data leaked into rescue CPIO'
    stale='[H12 HOST FAIL] stale guest HID evtest workload on disk'
    pass='[H12 HOST FIXTURE PASS] original signed Alpine H8 userland + signed Alpine evtest APK on immutable NVMe ext4; ELF-free rescue, exact inode/SHA evidence' ;;
  h13) prefix=H13; number=13;
    fixture='[H13 HOST FAIL] unknown, modified or incomplete existing mouse fixture'
    disk_tag='H13_MOUSE_SIGNED_ALPINE_DISK_ONLY_20261110'
    leak='[H13 HOST FAIL] original program/evtest data leaked into rescue CPIO'
    stale='[H13 HOST FAIL] stale guest mouse evtest workload on disk'
    pass='[H13 HOST FIXTURE PASS] original signed Alpine H8 userland + signed Alpine evtest APK on immutable NVMe ext4; ELF-free rescue, exact inode/SHA evidence' ;;
  h14) prefix=H14; number=14;
    fixture='[H14 HOST FAIL] unknown, modified or incomplete existing dual HID fixture'
    disk_tag='H14_DUAL_SIGNED_ALPINE_DISK_ONLY_20261110'
    leak='[H14 HOST FAIL] original program/evtest data leaked into rescue CPIO'
    stale='[H14 HOST FAIL] stale guest mouse evtest workload on disk'
    pass='[H14 HOST FIXTURE PASS] original signed Alpine H8 userland + signed Alpine evtest APK on immutable NVMe ext4; ELF-free rescue, exact inode/SHA evidence' ;;
  h15) prefix=H15; number=15;
    fixture='[H15 HOST FAIL] unknown, modified or incomplete existing dual HID fixture'
    disk_tag='H15_FTDI_SIGNED_ALPINE_DISK_ONLY_20261110'
    leak='[H15 HOST FAIL] original signed program data leaked into rescue CPIO'
    stale='[H15 HOST FAIL] stale guest FTDI BusyBox serial workload on disk'
    pass='[H15 HOST FIXTURE PASS] original signed Alpine H8 userland on immutable NVMe ext4; ELF-free rescue, exact inode/SHA evidence' ;;
  *) exit 2 ;;
esac
out=build/${profile}-qemu-only
image="$out/${profile}-alpine.ext4"
before="$out/${profile}-media-before.sha256"
originals="$out/${profile}-original-alpine-elfs.sha256"
tree="$out/fixture/disk-tree"
verify="$out/fixture/disk-verify"
rescue_root="$out/fixture/rescue-root"
rescue="$out/rescue.cpio"
h8_root=build/external/m39-packages/amd64/rootfs
signed_root=build/external/m166-batch/amd64/rootfs
for tool in docker sudo curl tar readelf sha256sum mke2fs debugfs \
            truncate cpio cmp grep find tr stat chmod; do
    command -v "$tool" >/dev/null || {
        echo "[${prefix} HOST FAIL] missing required $tool" >&2
        exit 1
    }
done
for path in "$image" "$before" "$originals" "$rescue"; do
    [ ! -L "$path" ] || {
        echo "[${prefix} HOST FAIL] symlink output refused: $path" >&2
        exit 2
    }
done
if [ -e "$image" ]; then
    [ -f "$image" ] && test -s "$before" &&
    test -s "$originals" && test -s "$rescue" &&
    (cd "$out" && sha256sum -c ${profile}-media-before.sha256 >/dev/null) || {
        echo "$fixture" >&2
        exit 1
    }
else
    [ ! -e "$tree" ] && [ ! -L "$tree" ] || {
        echo "[${prefix} HOST FAIL] foreign/stale staging tree; refusing overwrite" >&2
        exit 1
    }
    sh scripts/stage-h8-alpine-nvme.sh
    sh scripts/stage-alpine-display.sh amd64 m166-batch
    for guest_path in bin/busybox lib/ld-musl-x86_64.so.1 \
                      usr/bin/lua5.4 usr/bin/jq usr/bin/sqlite3; do
        test -s "$h8_root/$guest_path" && test -s "$signed_root/$guest_path" &&
        cmp "$h8_root/$guest_path" "$signed_root/$guest_path" || {
            echo "[${prefix} HOST FAIL] H8 baseline original changed: $guest_path" >&2
            exit 1
        }
    done
    grep -Fxq 'P:evtest' "$signed_root/lib/apk/db/installed" || {
        echo "[${prefix} HOST FAIL] signed Alpine evtest APK missing" >&2
        exit 1
    }
    test -x "$signed_root/usr/bin/evtest" &&
    readelf -h "$signed_root/usr/bin/evtest" |
        grep -Fq 'Advanced Micro Devices X86-64' || {
        echo "[${prefix} HOST FAIL] original evtest is missing/non-x86 ELF" >&2
        exit 1
    }
    mkdir -p "$tree" "$verify" "$rescue_root/etc" "$rescue_root/tmp"
    chmod 1777 "$rescue_root/tmp"
    cp -a "$signed_root/." "$tree/"
    mkdir -p "$tree/root"
    cp scripts/${profile}-guest-workload.sh "$tree/root/${profile}-workload.sh"
    chmod 755 "$tree/root/${profile}-workload.sh"
    printf '%s\n' "$disk_tag" \
        > "$tree/root/${profile}-disk-only.txt"
    cp "$tree/root/${profile}-disk-only.txt" "$tree/root/${profile}-copy.txt"
    printf '%s\n' "NaSeed ${prefix} rescue metadata only; no original Alpine ELF" \
        > "$rescue_root/etc/${profile}-rescue-only"
    (
        cd "$rescue_root"
        find . -print | cpio -o -H newc --quiet --owner=0:0
    ) > "$rescue.tmp"
    mv "$rescue.tmp" "$rescue"
    truncate -s 256M "$image"
    mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
        -U 01234567-89ab-4cde-8000-0000000000${number} \
        -d "$tree" "$image"
    chmod 0444 "$image"
    (cd "$out" && sha256sum ${profile}-alpine.ext4 > ${profile}-media-before.sha256)
    sha256sum "$h8_root/bin/busybox" "$tree/bin/busybox" \
        "$h8_root/usr/bin/lua5.4" "$tree/usr/bin/lua5.4" \
        "$h8_root/usr/bin/jq" "$tree/usr/bin/jq" \
        "$h8_root/usr/bin/sqlite3" "$tree/usr/bin/sqlite3" \
        "$signed_root/usr/bin/evtest" "$tree/usr/bin/evtest" \
        > "$originals"
fi
if cpio -it --quiet < "$rescue" |
   grep -Eq "(^|/)(busybox|ld-musl-x86_64\.so\.1|lua5\.4|jq|sqlite3|evtest|${profile}-workload\.sh|${profile}-disk-only\.txt)$"; then
    echo "$leak" >&2
    exit 1
fi
cpio -itv --quiet < "$rescue" |
    grep -Eq '^drwxrwxrwt[[:space:]]+.*[[:space:]]\.?/?tmp$' || {
    echo "[${prefix} HOST FAIL] ELF-free rescue lacks writable sticky RAM /tmp mountpoint" >&2
    exit 1
}
(cd "$out" && sha256sum -c ${profile}-media-before.sha256)
[ "$(stat -c '%a' "$image")" = 444 ] || {
    echo "[${prefix} HOST FAIL] NVMe media not protected readonly" >&2
    exit 1
}
sha256sum -c "$originals"
mkdir -p "$verify"
for guest_path in bin/busybox usr/bin/lua5.4 usr/bin/jq \
                  usr/bin/sqlite3 usr/bin/evtest \
                  root/${profile}-workload.sh root/${profile}-disk-only.txt; do
    dump_name=$(printf '%s' "$guest_path" | tr / _)
    debugfs -R "dump /$guest_path $verify/$dump_name" "$image" \
        >/dev/null 2>&1 || {
            echo "[${prefix} HOST FAIL] cannot read NVMe ext4 /$guest_path" >&2
            exit 1
        }
    cmp "$tree/$guest_path" "$verify/$dump_name" || {
        echo "[${prefix} HOST FAIL] NVMe ext4 inode mismatch: /$guest_path" >&2
        exit 1
    }
done
cmp scripts/${profile}-guest-workload.sh "$verify/root_${profile}-workload.sh" || {
    echo "$stale" >&2
    exit 1
}
echo "$pass"
