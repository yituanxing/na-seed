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
  if [ "$rc" -ne 1 ] ||
     ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
     ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
     ! grep -Fq '[SIGNAL] default SIGPIPE terminated pid=' "$clean" ||
     ! grep -Fq '[PROC] wait4 reaped pid=2 status=13' "$clean" ||
     grep -Fq '[DEADLOCK]' "$clean" ||
     grep -Fq '[FAULT]' "$clean"; then
    echo "[FAIL] early-closed pipe has no genuine SIGPIPE/reap: $case_name rc=$rc" >&2
    tail -95 "$clean" >&2
    exit 1
  fi
  case "$case_name" in
    shorthead)
      if ! grep -Fxq '256' "$clean"; then
        echo "[FAIL] truncated cat/head/wc did not transfer exactly 256 bytes" >&2
        tail -90 "$clean" >&2
        exit 1
      fi ;;
    yeshead)
      if [ "$(grep -Fxc 'Y' "$clean" || true)" -ne 3 ] ||
         ! grep -Fxq 'after-short' "$clean"; then
        echo "[FAIL] yes/head must emit exactly three Y lines and resume shell" >&2
        tail -90 "$clean" >&2
        exit 1
      fi ;;
    yeschain)
      if ! grep -Fxq '3' "$clean" ||
         ! grep -Fxq 'after-chain' "$clean"; then
        echo "[FAIL] yes/head/wc must count exactly three lines and resume shell" >&2
        tail -90 "$clean" >&2
        exit 1
      fi ;;
  esac
  echo "[ACCEPTED] real BusyBox $case_name SIGPIPE, Linux wait status 13, and shell continuation"
done
