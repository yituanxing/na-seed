#!/bin/sh
# Discover which real BusyBox ash producer/consumer lifecycle paths need
# signal or wakeup work before declaring a new milestone accepted.
set -eu
for case_name in shorthead yeshead yeschain; do
  case "$case_name" in
    shorthead) args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox head -c 256 | /bin/busybox wc -c'" ;;
    yeshead) args="sh -c '/bin/busybox yes Y | /bin/busybox head -n 3; echo after-short'" ;;
    yeschain) args="sh -c '/bin/busybox yes ok | /bin/busybox head -n 3 | /bin/busybox wc -l; echo after-chain'" ;;
  esac
  stage="build/ash-life-$case_name-iso"
  iso="build/naseed-ash-life-$case_name.iso"
  log="build/ash-life-$case_name-serial.log"
  clean="build/ash-life-$case_name-clean.log"
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
  timeout 12s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
  tr -d '\r' < "$log" > "$clean"
  echo "[LIFE-DIAG] case=$case_name qemu_rc=$rc args=$args"
  grep -E '^\[(DEADLOCK|FAIL|FAULT|EXIT|PASS|SCHED|PROC|MISS)\]|^(256|3|Y|after-short|after-chain)$' "$clean" | tail -65 || true
  grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" || {
      echo "[FAIL] archive did not mount in $case_name" >&2
      tail -80 "$clean" >&2
      exit 1
  }
done
