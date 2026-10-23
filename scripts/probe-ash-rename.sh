#!/bin/sh
# M6.9: genuine pinned Alpine BusyBox cp/mv, no modified applet code.
# Each ISO is fresh: exact stdout, shell status and QEMU exit are checked.
set -eu
for scenario in copy copylarge move replace pinned dirtree cwdmove noreplace readonly quota; do
    case "$scenario" in
        copy)
            args="sh -c 'cp /etc/hostname /tmp/copied && cat /tmp/copied'"
            expect=naseed; also=naseed ;;
        copylarge)
            args="sh -c 'yes X | head -c 8192 > /tmp/large; cp /tmp/large /tmp/other && wc -c < /tmp/other'"
            expect=8192; also=8192 ;;
        move)
            args="sh -c 'echo moved > /tmp/from; mv /tmp/from /tmp/to && cat /tmp/to && echo move-ok'"
            expect=move-ok; also=moved ;;
        replace)
            args="sh -c 'echo original > /tmp/one; echo old > /tmp/two; mv /tmp/one /tmp/two && cat /tmp/two && echo replace-ok'"
            expect=replace-ok; also=original ;;
        pinned)
            args="sh -c 'echo original > /tmp/one; echo former > /tmp/two; exec 3< /tmp/two; mv /tmp/one /tmp/two && cat <&3 && cat /tmp/two && echo pinned-ok'"
            expect=pinned-ok; also=former ;;
        dirtree)
            args="sh -c 'mkdir -p /tmp/tree/deep && echo nested > /tmp/tree/deep/file && mv /tmp/tree /tmp/newtree && cat /tmp/newtree/deep/file && rm -r /tmp/newtree && echo tree-ok'"
            expect=tree-ok; also=nested ;;
        cwdmove)
            args="sh -c 'mkdir -p /tmp/tree/deep; cd /tmp/tree/deep; mv /tmp/tree /tmp/newtree; pwd -P; cd /tmp; rm -r newtree; echo cwd-ok'"
            expect=cwd-ok; also=/tmp/newtree/deep ;;
        noreplace)
            args="sh -c 'echo keep > /tmp/to; echo incoming > /tmp/from; mv -n /tmp/from /tmp/to; cat /tmp/to; echo noreplace-ok'"
            expect=noreplace-ok; also=keep ;;
        readonly)
            args="sh -c 'echo keep > /tmp/from; mv /tmp/from /etc/target && echo wrong || echo readonly; cat /tmp/from; cat /etc/hostname'"
            expect=readonly; also=keep ;;
        quota)
            # A real build needs >8 simultaneous inodes; verify that the
            # ninth and sixteenth files succeed through a second slab.
            # Keep the historical quota scenario name for CI stability.
            args="sh -c 'for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do echo x > /tmp/file\$i || exit 1; done; cat /tmp/file1; cat /tmp/file16; echo grown; echo quota-ok'"
            expect=quota-ok; also=grown ;;
    esac
    stage="build/ash-rename-$scenario-iso"
    iso="build/naseed-ash-rename-$scenario.iso"
    log="build/ash-rename-$scenario-serial.log"
    clean="build/ash-rename-$scenario-clean.log"
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
    timeout 25s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fxq "$expect" "$clean" ||
       ! grep -Fxq "$also" "$clean" ||
       grep -Fxq wrong "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] real BusyBox cp/mv scenario=$scenario rc=$rc expected=$expect/$also" >&2
        tail -110 "$clean" >&2
        exit 1
    fi
    case "$scenario" in
        copylarge)
            ! grep -Fxq 0 "$clean" || {
                echo '[FAIL] large copy silently wrote zero bytes' >&2
                exit 1
            } ;;
        readonly)
            grep -Fxq naseed "$clean" || {
                echo '[FAIL] immutable CPIO data changed or inaccessible' >&2
                exit 1
            } ;;
        pinned)
            grep -Fxq original "$clean" || {
                echo '[FAIL] rename overwrite did not preserve source inode' >&2
                exit 1
            } ;;
        quota)
            grep -Fxq x "$clean" || {
                echo '[FAIL] demand-paged tmpfs inode slab lost the first or last file' >&2
                exit 1
            } ;;
    esac
    echo "[ACCEPTED] unchanged BusyBox cp/mv inode scenario=$scenario"
done
