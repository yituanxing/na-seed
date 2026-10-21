#!/bin/sh
# Original pinned Alpine BusyBox ash exercising shared pipe descriptions,
# repeated allocation/close, read EOF and multi-stage pipeline scheduling.
# No fake BusyBox or kernel-side expected-output shortcuts.
set -eu
for scenario in reuse cascade readback; do
    case "$scenario" in
        reuse)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo abc | wc -c; done; echo pipe-reuse-ok'"
            expected=pipe-reuse-ok; value=4; count=12 ;;
        cascade)
            args="sh -c 'echo abc | cat | wc -c; echo pipe-cascade-ok'"
            expected=pipe-cascade-ok; value=4; count=1 ;;
        readback)
            args="sh -c 'for i in 1 2 3 4 5 6 7 8; do echo line | cat > /tmp/message; cat /tmp/message; done; echo pipe-readback-ok'"
            expected=pipe-readback-ok; value=line; count=8 ;;
    esac
    stage="build/ash-pipe-backend-$scenario-iso"
    iso="build/naseed-ash-pipe-backend-$scenario.iso"
    log="build/ash-pipe-backend-$scenario-serial.log"
    clean="build/ash-pipe-backend-$scenario-clean.log"
    rm -rf "$stage"
    cp -R build/alpine-archive-iso "$stage"
    awk -v args="$args" '/^    cmdline: / {
        print "    cmdline: naseed.argv=/bin/busybox " args
        next
    } { print }' limine-alpine-archive.conf >"$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 35s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' <"$log" >"$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expected" "$clean" ||
       [ "$(grep -Fxc "$value" "$clean" || true)" -ne "$count" ] ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] real BusyBox pipe backend scenario=$scenario rc=$rc expected=$count" >&2
        tail -120 "$clean" >&2
        exit 1
    fi
    echo "[ACCEPTED] authentic BusyBox pipe backend scenario=$scenario"
done
