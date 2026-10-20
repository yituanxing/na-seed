#!/bin/sh
# Same official Alpine BusyBox and musl runtime; change only Limine cmdline.
# Each case has an independent Linux-observable stdout and exit acceptance.
set -eu
base=build/alpine-iso
for testname in uname pwd id; do
    case "$testname" in
        uname) args='uname -s'; expected='Linux' ;;
        pwd)   args='pwd'; expected='/' ;;
        id)    args='id -u'; expected='0' ;;
    esac
    stage="build/alpine-argv-$testname-iso"
    iso="build/naseed-alpine-argv-$testname.iso"
    log="build/alpine-argv-$testname-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    # Deliberately reuse source config's module list and command; only
    # inject the test-specific kernel cmdline immediately below kernel path.
    sed "/^    path: boot():\\/boot\\/kernel$/a\\
    cmdline: naseed.argv=/bin/busybox $args" \
        limine-alpine.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1

    rc=0
    timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=/bin/busybox $args" "$log" ||
       ! grep -Fq '[EXIT] user status=0' "$log" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$log" ||
       ! tr -d '\r' < "$log" | grep -Fxq "$expected"; then
        echo "[FAIL] Alpine BusyBox $args: expected '$expected', real exit 0" >&2
        tail -75 "$log" >&2
        exit 1
    fi
    echo "[ACCEPTED] Official Alpine BusyBox $args: stdout '$expected' and exit 0"
done
