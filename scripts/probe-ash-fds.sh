#!/bin/sh
# M7.1: real, pinned Alpine BusyBox ash exercising descriptor-vs-open-file
# ownership across dup, fork/exec, unlink/recreate and final-close recycling.
set -eu
for scenario in shared dup inherited unlink finalclose; do
    case "$scenario" in
        shared)
            args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; cat <&3; cat <&3; echo shared-ok'"
            expect=shared-ok; payload=abc; once=abc ;;
        dup)
            args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; exec 4<&3; cat <&4; cat <&3; echo dup-ok'"
            expect=dup-ok; payload=abc; once=abc ;;
        inherited)
            args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; sh -c "cat <&3"; cat <&3; echo inherited-ok'"
            expect=inherited-ok; payload=abc; once=abc ;;
        unlink)
            args="sh -c 'echo original > /tmp/f; exec 3< /tmp/f; rm /tmp/f; echo replacement > /tmp/f; cat <&3; cat /tmp/f; echo unlink-ok'"
            expect=unlink-ok; payload=original; once=original ;;
        finalclose)
            args="sh -c 'echo abc > /tmp/f; exec 3< /tmp/f; exec 3<&-; rm /tmp/f; echo recycled > /tmp/f; cat /tmp/f; echo close-ok'"
            expect=close-ok; payload=recycled; once=recycled ;;
    esac
    stage="build/ash-fd-$scenario-iso"
    iso="build/naseed-ash-fd-$scenario.iso"
    log="build/ash-fd-$scenario-serial.log"
    clean="build/ash-fd-$scenario-clean.log"
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
    timeout 22s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expect" "$clean" ||
       ! grep -Fxq "$payload" "$clean" ||
       [ "$(grep -Fxc "$once" "$clean" || true)" -ne 1 ] ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] original BusyBox descriptor scenario=$scenario rc=$rc" >&2
        tail -120 "$clean" >&2
        exit 1
    fi
    if [ "$scenario" = unlink ]; then
        grep -Fxq replacement "$clean" || {
            echo '[FAIL] recreated pathname did not get independent bytes' >&2
            exit 1
        }
    fi
    echo "[ACCEPTED] real BusyBox FD description lifetime scenario=$scenario"
done
