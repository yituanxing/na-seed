#!/bin/sh
# Confirm the historic *archive-only* Limine configuration without any
# naseed.argv still boots the genuine Alpine BusyBox default program.
# Also confirm an explicit argv chooses the real archive-backed ELF.
set -eu
for case in default explicit; do
    stage="build/boot-handoff-$case-iso"
    iso="build/naseed-boot-handoff-$case.iso"
    log="build/boot-handoff-$case-serial.log"
    clean="build/boot-handoff-$case-clean.log"
    rm -rf "$stage"
    cp -R build/alpine-archive-iso "$stage"
    if [ "$case" = default ]; then
        awk '!/^    cmdline: /' limine-alpine-archive.conf >"$stage/boot/limine/limine.conf"
        success='[PASS] M4 Alpine dynamic BusyBox echo hello'
        output=hello
    else
        awk '/^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox cat /etc/hostname"
            next
        } { print }' limine-alpine-archive.conf >"$stage/boot/limine/limine.conf"
        success='[PASS] cmdline userspace exit 0'
        output=naseed
    fi
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 20s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' <"$log" >"$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[M5] CPIO executable selected: /bin/busybox' "$clean" ||
       ! grep -Fq '[DYN] real musl interpreter loaded' "$clean" ||
       ! grep -Fxq "$output" "$clean" ||
       ! grep -Fq "$success" "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean" ||
       grep -Fq '[FAULT]' "$clean"; then
        echo "[FAIL] original archive-only Limine boot handoff case=$case rc=$rc" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    echo "[ACCEPTED] genuine initramfs CPIO boot handoff case=$case"
done
