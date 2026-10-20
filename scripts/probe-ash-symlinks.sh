#!/bin/sh
# M6.6: real CPIO S_IFLNK entries, no artificial /bin/app command rewrites.
# Assert original BusyBox stdout, Linux execve, and pipe/scheduler behavior.
set -eu
for scenario in barecat smallpipe largepipe hash readlink lslink; do
    case "$scenario" in
        barecat) args="sh -c 'cat /etc/hostname'"
                 expected=naseed ;;
        smallpipe) args="sh -c 'cat /etc/hostname | wc -c'"
                   expected=7 ;;
        largepipe) args="sh -c 'cat /bin/busybox | wc -c'"
                   expected=$(wc -c < build/alpine/bin/busybox | tr -d ' ') ;;
        hash) args="sh -c 'cat /bin/busybox | sha256sum'"
              expected=$(sha256sum build/alpine/bin/busybox | cut -d ' ' -f 1) ;;
        readlink) args="sh -c 'readlink /bin/cat'"
                  expected=busybox ;;
        lslink) args="sh -c 'ls -l /bin/cat'"
                expected='cat -> busybox' ;;
    esac
    stage="build/ash-link-$scenario-iso"
    iso="build/naseed-ash-link-$scenario.iso"
    log="build/ash-link-$scenario-serial.log"
    clean="build/ash-link-$scenario-clean.log"
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
    timeout 25s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] real bare BusyBox applet scenario=$scenario rc=$rc" >&2
        tail -100 "$clean" >&2
        exit 1
    fi
    if [ "$scenario" = hash ] || [ "$scenario" = lslink ]; then
        grep -Fq "$expected" "$clean" || {
            echo "[FAIL] $scenario expected $expected" >&2
            tail -100 "$clean" >&2
            exit 1
        }
    else
        grep -Fxq "$expected" "$clean" || {
            echo "[FAIL] $scenario expected exact stdout $expected" >&2
            tail -100 "$clean" >&2
            exit 1
        }
    fi
    if [ "$scenario" = largepipe ] || [ "$scenario" = hash ]; then
        grep -Fq '[SCHED] blocked pid=' "$clean" || {
            echo '[FAIL] large bare-app pipeline bypassed blocking' >&2
            exit 1
        }
    fi
    if [ "$scenario" = barecat ] &&
       ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/cat' "$clean"; then
        echo '[FAIL] bare cat did not enter execve through a symlink' >&2
        exit 1
    fi
    echo "[ACCEPTED] authentic BusyBox ash bare-app $scenario"
done
