#!/bin/sh
# Same one-module archive, two distinct official Linux BusyBox ELF binaries.
# Selection is ONLY through the real Limine userspace argv[0].
set -eu
base=build/alpine-archive-iso
for testcase in dynamic static missing nonexec; do
    case "$testcase" in
        dynamic)
            args='/bin/busybox echo alpine'
            selected='/bin/busybox'
            expected='alpine'
            good=1 ;;
        static)
            args='/usr/bin/busybox echo debian'
            selected='/usr/bin/busybox'
            expected='debian'
            good=1 ;;
        missing)
            args='/bin/does-not-exist echo no'
            expected='[FAIL] M1 CPIO executable path not found'
            good=0 ;;
        nonexec)
            args='/etc/hostname echo no'
            expected='[FAIL] M1 CPIO selected path is not an executable regular file'
            good=0 ;;
    esac
    stage="build/alpine-selector-$testcase-iso"
    iso="build/naseed-alpine-selector-$testcase.iso"
    log="build/alpine-selector-$testcase-serial.log"
    clean="build/alpine-selector-$testcase-clean.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    cp build/alpine-selector-rootfs.cpio "$stage/boot/rootfs.cpio"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=" args
            next
        }
        { print }
    ' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    if [ "$good" -eq 1 ]; then
        timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    else
        timeout 5s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    fi
    tr -d '\r' < "$log" > "$clean"
    if ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean"; then
        echo "[FAIL] archive did not mount: $args" >&2
        tail -100 "$clean" >&2
        exit 1
    fi
    if [ "$good" -eq 1 ]; then
        if [ "$rc" -ne 1 ] ||
           ! grep -Fxq "$expected" "$clean" ||
           ! grep -Fq "[M5] CPIO executable selected: $selected" "$clean" ||
           ! grep -Fq '[EXIT] user status=0' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
            echo "[FAIL] ELF selection / stdout / exit mismatch: $args" >&2
            tail -100 "$clean" >&2
            exit 1
        fi
        echo "[ACCEPTED] CPIO argv0=$selected, real BusyBox stdout=$expected"
    else
        if [ "$rc" -eq 1 ] ||
           ! grep -Fq "$expected" "$clean" ||
           grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
            echo "[FAIL] invalid CPIO executable unexpectedly accepted: $args" >&2
            tail -100 "$clean" >&2
            exit 1
        fi
        echo "[REJECTED] invalid archive executable: $args"
    fi
done
