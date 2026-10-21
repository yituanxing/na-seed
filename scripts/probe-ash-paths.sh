#!/bin/sh
# M7.2: unchanged Alpine BusyBox ash and symlink applets exercise the
# syscall path builder and CPIO component walk. No substituted test shell.
set -eu
for scenario in symlink-dotdot relative-cpio relative-tmp symlink-final parent-error; do
    case "$scenario" in
        symlink-dotdot)
            args="sh -c 'if cat /bin/cat/../busybox > /dev/null 2>&1; then echo wrong; else echo enotdir; fi; cat /etc/hostname'"
            expect=enotdir; also=naseed ;;
        relative-cpio)
            args="sh -c 'cd /etc && cat ../etc/hostname && echo archive-path-ok'"
            expect=archive-path-ok; also=naseed ;;
        relative-tmp)
            args="sh -c 'cd /tmp && mkdir d && cd d && echo scratch > ../file && cat ../file && echo tmp-path-ok'"
            expect=tmp-path-ok; also=scratch ;;
        symlink-final)
            args="sh -c 'readlink /bin/cat && cat /etc/hostname && echo link-final-ok'"
            expect=link-final-ok; also=busybox ;;
        parent-error)
            args="sh -c 'if cat /etc/hostname/../hostname > /dev/null 2>&1; then echo wrong; else echo notdir; fi; cat /etc/hostname'"
            expect=notdir; also=naseed ;;
    esac
    stage="build/ash-path-$scenario-iso"
    iso="build/naseed-ash-path-$scenario.iso"
    log="build/ash-path-$scenario-serial.log"
    clean="build/ash-path-$scenario-clean.log"
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
    timeout 22s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' <"$log" >"$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expect" "$clean" ||
       ! grep -Fxq "$also" "$clean" ||
       grep -Fxq wrong "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] BusyBox pathname semantics scenario=$scenario rc=$rc" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    echo "[ACCEPTED] original BusyBox pathname scenario=$scenario"
done
