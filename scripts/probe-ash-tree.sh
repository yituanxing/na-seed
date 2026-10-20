#!/bin/sh
# Real, unchanged Alpine BusyBox ash + mkdir/rm/rmdir/ls/cat; all QEMU boots.
# Acceptance requires output, zero shell status, and old CPIO kept read-only.
set -eu
for scenario in lifecycle nested cwd listing nonempty live readonly missing; do
    case "$scenario" in
        lifecycle)
            args="sh -c 'mkdir /tmp/work && echo hello > /tmp/work/test && cat /tmp/work/test && rm /tmp/work/test && rmdir /tmp/work && echo removed'"
            expect='removed'; also='hello' ;;
        nested)
            args="sh -c 'mkdir -p /tmp/work/deep && echo nested > /tmp/work/deep/file && cat /tmp/work/deep/file && rm -r /tmp/work && echo recursive-ok'"
            expect='recursive-ok'; also='nested' ;;
        cwd)
            args="sh -c 'mkdir /tmp/d && cd /tmp/d && echo cwd > file && cat file && cd /tmp && rm -r d && echo cwd-ok'"
            expect='cwd-ok'; also='cwd' ;;
        listing)
            args="sh -c 'mkdir /tmp/d && echo data > /tmp/d/entry && ls /tmp/d && rm -r /tmp/d && echo listed'"
            expect='listed'; also='entry' ;;
        nonempty)
            args="sh -c 'mkdir /tmp/d && echo busy > /tmp/d/f && if rmdir /tmp/d; then echo wrong; else echo nonempty; fi; rm /tmp/d/f && rmdir /tmp/d && echo empty-removed'"
            expect='empty-removed'; also='nonempty' ;;
        live)
            args="sh -c 'echo original > /tmp/a; exec 3< /tmp/a; rm /tmp/a && echo replacement > /tmp/a && cat <&3 && cat /tmp/a && echo live-ok'"
            expect='live-ok'; also='original' ;;
        readonly)
            args="sh -c 'mkdir /etc/newdir && echo wrong || echo readonly; rm /etc/hostname && echo wrong || echo readonly-rm; cat /etc/hostname'"
            expect='readonly-rm'; also='naseed' ;;
        missing)
            args="sh -c 'if rm /tmp/no-such-file; then echo wrong; else echo enoent; fi; echo missing-ok'"
            expect='missing-ok'; also='enoent' ;;
    esac
    stage="build/ash-tree-$scenario-iso"
    iso="build/naseed-ash-tree-$scenario.iso"
    log="build/ash-tree-$scenario-serial.log"
    clean="build/ash-tree-$scenario-clean.log"
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
    timeout 20s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' <"$log" >"$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expect" "$clean" ||
       ! grep -Fxq "$also" "$clean" ||
       grep -Fxq wrong "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] real BusyBox directory/lifetime case=$scenario rc=$rc" >&2
        tail -105 "$clean" >&2
        exit 1
    fi
    if [ "$scenario" = live ]; then
        grep -Fxq replacement "$clean" || {
            echo '[FAIL] unlink+recreate lost the new inode' >&2
            exit 1
        }
    fi
    echo "[ACCEPTED] real BusyBox tmp tree/lifecycle scenario=$scenario"
done
