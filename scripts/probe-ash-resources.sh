#!/bin/sh
# M7.5: repeated actual Alpine BusyBox ash workloads test resource *reuse*
# across fork/wait4, syscall-boundary scheduling, shared FD descriptions,
# anonymous pipes, and the volatile inode tombstone pool.
set -eu
for scenario in pipeline scratch subshell tombstone sigpipe; do
    case "$scenario" in
        pipeline)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo abc | cat | wc -c; done; echo lifecycle-pipeline-ok'"
            final=lifecycle-pipeline-ok; value=4; count=12 ;;
        scratch)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo source > /tmp/item; cat /tmp/item | wc -c; rm /tmp/item; done; echo lifecycle-scratch-ok'"
            final=lifecycle-scratch-ok; value=7; count=12 ;;
        subshell)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do ( echo child ) > /tmp/item; cat /tmp/item; rm /tmp/item; done; echo lifecycle-subshell-ok'"
            final=lifecycle-subshell-ok; value=child; count=12 ;;
        tombstone)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do echo original > /tmp/item; exec 3< /tmp/item; rm /tmp/item; echo replacement > /tmp/item; cat <&3; exec 3<&-; rm /tmp/item; done; echo lifecycle-tombstone-ok'"
            final=lifecycle-tombstone-ok; value=original; count=10 ;;
        sigpipe)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8; do yes X | head -c 2; done; echo lifecycle-sigpipe-ok'"
            final=lifecycle-sigpipe-ok; value=X; count=8 ;;
    esac
    stage="build/ash-resource-$scenario-iso"
    iso="build/naseed-ash-resource-$scenario.iso"
    log="build/ash-resource-$scenario-serial.log"
    clean="build/ash-resource-$scenario-clean.log"
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
    timeout 55s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    actual="$(grep -Fxc "$value" "$clean" || true)"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$final" "$clean" ||
       [ "$actual" -ne "$count" ] ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] real BusyBox repeated resource scenario=$scenario rc=$rc count=$actual expected=$count" >&2
        tail -160 "$clean" >&2
        exit 1
    fi
    echo "[ACCEPTED] original BusyBox resource reuse scenario=$scenario cycles=$count"
done
