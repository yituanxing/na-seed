#!/bin/sh
# M6.7 real BusyBox ash redirection: bounded volatile /tmp, original Alpine ELF.
# QEMU stdout, process exit, CPIO mount and errno are independent acceptance.
set -eu
for scenario in create append truncate pipeline copy listing shared readonly; do
    case "$scenario" in
        create) args="sh -c 'echo scratch > /tmp/message; cat /tmp/message'"
                expect=scratch ;;
        append) args="sh -c 'echo alpha > /tmp/log; echo beta >> /tmp/log; cat /tmp/log'"
                expect=beta ;;
        truncate) args="sh -c 'echo old > /tmp/log; echo fresh > /tmp/log; cat /tmp/log'"
                  expect=fresh ;;
        pipeline) args="sh -c 'echo abc > /tmp/f; cat /tmp/f | wc -c'"
                  expect=4 ;;
        copy) args="sh -c 'echo persisted > /tmp/source; cat /tmp/source > /tmp/copy; cat /tmp/copy'"
              expect=persisted ;;
        listing) args="sh -c 'echo data > /tmp/entry; ls /tmp'"
                 expect=entry ;;
        shared) args="sh -c 'echo abc > /tmp/entry; exec 3< /tmp/entry; read one <&3; read two <&3; echo \$one:\$two'"
                expect=abc: ;;
        readonly) args="sh -c 'echo corrupt > /etc/hostname || echo readonly; cat /etc/hostname'"
                  expect=naseed ;;
    esac
    stage="build/ash-tmp-$scenario-iso"
    iso="build/naseed-ash-tmp-$scenario.iso"
    log="build/ash-tmp-$scenario-serial.log"
    clean="build/ash-tmp-$scenario-clean.log"
    rm -rf "$stage"
    cp -R build/alpine-archive-iso "$stage"
    awk -v args="$args" '/^    cmdline: / {
        print "    cmdline: naseed.argv=/bin/busybox " args
        next
    } { print }' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 20s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expect" "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] BusyBox tmp/$scenario expected=$expect rc=$rc" >&2
        tail -105 "$clean" >&2
        exit 1
    fi
    case "$scenario" in
        append)
            grep -Fxq alpha "$clean" || exit 1
            [ "$(grep -Fxc alpha "$clean" || true)" -eq 1 ] || exit 1 ;;
        truncate)
            if grep -Fxq old "$clean"; then
                echo '[FAIL] O_TRUNC left old bytes visible' >&2
                exit 1
            fi ;;
        readonly)
            grep -Fxq readonly "$clean" || {
                echo '[FAIL] CPIO unexpectedly writable' >&2
                exit 1
            } ;;
        copy)
            grep -Fq '[PROC] fork parent=1 child=' "$clean" || {
                echo '[FAIL] copy never executed a child process' >&2
                exit 1
            } ;;
    esac
    echo "[ACCEPTED] unmodified BusyBox ash writable /tmp scenario=$scenario"
done
