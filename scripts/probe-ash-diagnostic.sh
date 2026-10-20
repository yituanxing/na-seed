#!/bin/sh
# Discover missing *classes* of Linux process/shell semantics from the
# unmodified Alpine BusyBox ash on an archive-only NaSeed boot.
# Diagnostic only: a printed failure is NOT an accepted new capability.
set -eu
base=build/alpine-archive-iso
for scenario in builtin builtinpwd applet external pipeline; do
    case "$scenario" in
        builtin) args="sh -c 'echo shell-ready'" ;;
        builtinpwd) args="sh -c 'pwd'" ;;
        applet) args="sh -c 'cat /etc/hostname'" ;;
        external) args="sh -c '/bin/busybox cat /etc/hostname'" ;;
        pipeline) args="sh -c 'echo abc | wc -c'" ;;
    esac
    stage="build/ash-diag-$scenario-iso"
    iso="build/naseed-ash-diag-$scenario.iso"
    log="build/ash-diag-$scenario-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox " args
            next
        }
        { print }
    ' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    timeout 4s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    echo "[ASH-DIAG] case=$scenario qemu_status=$rc args=$args"
    tr -d '\r' < "$log" | grep -E '^\[(MISS|FAULT|FAIL|EXIT|PASS)\]|^(shell-ready|naseed|/|4)$' || true
    if ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"; then
        echo "[ASH-DIAG-FAIL] missing actual archive mount" >&2
        exit 1
    fi
done
