#!/bin/sh
# M5.4 acceptance: unmodified Alpine BusyBox ash calls Linux execve(59).
# No rewritten user binary, no boot-cmdline swap after entry, no fake kernel
# syscall result. The observed second program must print real CPIO contents.
set -eu
base=build/alpine-archive-iso
for scenario in cat echo execbuiltin missing; do
    case "$scenario" in
        cat) args="sh -c '/bin/busybox cat /etc/hostname'"
             expect='naseed'
             execpath='/bin/busybox'
             good=yes ;;
        echo) args="sh -c '/bin/busybox echo exec-replaced'"
             expect='exec-replaced'
             execpath='/bin/busybox'
             good=yes ;;
        execbuiltin) args="sh -c 'exec /bin/busybox cat /etc/hostname'"
             expect='naseed'
             execpath='/bin/busybox'
             good=yes ;;
        missing) args="sh -c 'exec /bin/does-not-exist'"
             expect='[EXIT] user status=127'
             good=no ;;
    esac
    stage="build/ash-exec-$scenario-iso"
    iso="build/naseed-ash-exec-$scenario.iso"
    log="build/ash-exec-$scenario-serial.log"
    clean="build/ash-exec-$scenario-clean.log"
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
    timeout 10s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$good" = yes ]; then
        if [ "$rc" -ne 1 ] ||
           ! grep -Fxq "$expect" "$clean" ||
           ! grep -Fq '[EXIT] user status=0' "$clean" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
            echo "[FAIL] real ash execve scenario=$scenario" >&2
            tail -120 "$clean" >&2
            exit 1
        fi
        if ! grep -Fq "[EXEC] replaced process with CPIO ELF: $execpath" "$clean"; then
            echo '[FAIL] syscall execve did not replace the image' >&2
            tail -120 "$clean" >&2
            exit 1
        fi
    else
        # A real shell exec builtin exits 127 when Linux execve returns
        # ENOENT. NaSeed's diagnostic fail-stop for nonzero boot program
        # exit requires the timeout; it must NOT log image replacement.
        if [ "$rc" -ne 124 ] ||
           ! grep -Fq 'does-not-exist' "$clean" ||
           ! grep -Fq "$expect" "$clean" ||
           ! grep -Fq '[EXEC] Linux execve rejected errno=2' "$clean" ||
           ! grep -Fq '[FAIL] M1 userspace exited nonzero' "$clean" ||
           grep -Fq '[EXEC] replaced process with CPIO ELF:' "$clean" ||
           grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
            echo '[FAIL] missing executable did not return real ENOENT to ash' >&2
            tail -120 "$clean" >&2
            exit 1
        fi
    fi
    echo "[ACCEPTED] original BusyBox ash real execve scenario=$scenario"
done
