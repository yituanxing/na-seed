#!/bin/sh
# Black-box acceptance of unmodified Alpine BusyBox with genuine archive data.
set -eu
# Use the same real-ELF matrix for legacy module+CPIO and CPIO-only boot.
base=${NASEED_FS_BASE:-build/alpine-fs-iso}
config=${NASEED_FS_CONFIG:-limine-alpine-fs.conf}
prefix=${NASEED_FS_PREFIX:-alpine-fs}
for testcase in lsroot lsbin cat find wc grep head; do
    case "$testcase" in
        lsroot) args='ls -la /' ;;
        lsbin)  args='ls /bin' ;;
        cat)    args='cat /etc/hostname' ;;
        find)   args='find /etc -type f' ;;
        wc)     args='wc -c /etc/hostname' ;;
        grep)   args='grep -n naseed /etc/hostname' ;;
        head)   args='head -n 1 /etc/hostname' ;;
    esac
    stage="build/$prefix-$testcase-iso"
    iso="build/naseed-$prefix-$testcase.iso"
    log="build/$prefix-$testcase-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox " args
            next
        }
        { print }
    ' "$config" > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    clean="build/$prefix-$testcase-clean.log"
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
        cat|head)
            grep -Fxq naseed "$clean" || {
                echo '[FAIL] text command did not return actual hostname bytes' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
        find)
            grep -Fxq /etc/hostname "$clean" || {
                echo '[FAIL] find did not traverse the /etc directory' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
        wc)
            grep -Eq '^[[:space:]]*7[[:space:]]+/etc/hostname$' "$clean" || {
                echo '[FAIL] wc -c not equal to actual 7-byte file' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
        grep)
            grep -Fxq 1:naseed "$clean" || {
                echo '[FAIL] grep did not parse the file content' >&2
                tail -110 "$clean" >&2
                exit 1
            } ;;
    esac
    echo "[ACCEPTED] Alpine BusyBox $args (real QEMU, archive-backed)"
done
