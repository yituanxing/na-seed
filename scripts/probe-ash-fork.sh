#!/bin/sh
# Native upstream Alpine BusyBox ash must fork a real child, execute an ELF,
# wait4/reap its PID, and continue in the original parent process.
set -eu
base=build/alpine-archive-iso
for scenario in sequence repeat cwd; do
    case "$scenario" in
        sequence)
            args="sh -c '/bin/busybox cat /etc/hostname; echo finished'"
            expected='naseed finished'
            forks=1 ;;
        repeat)
            args="sh -c '/bin/busybox echo child-one; /bin/busybox echo child-two; echo parent-after'"
            expected='child-one child-two parent-after'
            forks=2 ;;
        cwd)
            args="sh -c 'cd /etc; /bin/busybox cat hostname; echo after-cwd'"
            expected='naseed after-cwd'
            forks=1 ;;
    esac
    stage="build/ash-fork-$scenario-iso"
    iso="build/naseed-ash-fork-$scenario.iso"
    log="build/ash-fork-$scenario-serial.log"
    clean="build/ash-fork-$scenario-clean.log"
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
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fq '[EXIT] user status=0' "$clean"; then
        echo "[FORK-FAIL] QEMU exit or shell result: $scenario" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    got_forks=$(grep -Fc '[PROC] fork parent=1 child=' "$clean" || true)
    got_waits=$(grep -Fc '[PROC] wait4 reaped pid=' "$clean" || true)
    if [ "$got_forks" -ne "$forks" ] || [ "$got_waits" -ne "$forks" ]; then
        echo "[FORK-FAIL] expected $forks forks+wait4; got $got_forks + $got_waits" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    for word in $expected; do
        if ! grep -Fxq "$word" "$clean"; then
            echo "[FORK-FAIL] $scenario missing user stdout $word" >&2
            tail -110 "$clean" >&2
            exit 1
        fi
    done
    echo "[ACCEPTED] real BusyBox ash fork/exec/wait4 scenario=$scenario"
done
