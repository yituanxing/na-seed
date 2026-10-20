#!/bin/sh
# Black-box acceptance of unmodified Alpine BusyBox with genuine archive data.
set -eu
base=build/alpine-fs-iso
for testcase in lsroot lsbin cat; do
    case "$testcase" in
        lsroot) args='ls -la /' ;;
        lsbin)  args='ls /bin' ;;
        cat)    args='cat /etc/hostname' ;;
    esac
    stage="build/alpine-fs-$testcase-iso"
    iso="build/naseed-alpine-fs-$testcase.iso"
    log="build/alpine-fs-$testcase-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox " args
            next
        }
        { print }
    ' limine-alpine-fs.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    clean="build/alpine-fs-$testcase-clean.log"
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=/bin/busybox $args" "$clean" ||
       ! grep -Fq '[EXIT] user status=0' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
        echo "[FAIL] real BusyBox $args did not exit successfully" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    case "$testcase" in
        lsroot)
            for entry in bin etc lib; do
                if ! grep -Eq "[[:space:]]$entry$" "$clean"; then
                    echo "[FAIL] ls -la / missing $entry" >&2
                    tail -110 "$clean" >&2
                    exit 1
                fi
            done ;;
        lsbin)
            grep -Fxq busybox "$clean" || {
                echo '[FAIL] ls /bin missing busybox' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
        cat)
            grep -Fxq naseed "$clean" || {
                echo '[FAIL] cat /etc/hostname did not return archive bytes' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
    esac
    echo "[ACCEPTED] Alpine BusyBox $args (real QEMU, archive-backed)"
done
