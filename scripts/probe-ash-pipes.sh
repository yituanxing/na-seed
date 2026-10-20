#!/bin/sh
# Real unmodified BusyBox ash exercises Linux anonymous pipes and POSIX EOF.
set -eu
for testcase in builtin external chain; do
  case "$testcase" in
    builtin)
      args="sh -c 'echo abc | /bin/busybox wc -c'"
      expected=4 ;;
    external)
      args="sh -c '/bin/busybox cat /etc/hostname | /bin/busybox wc -c'"
      expected=7 ;;
    chain)
      args="sh -c 'echo test | /bin/busybox cat | /bin/busybox wc -c'"
      expected=5 ;;
  esac
  stage="build/ash-pipe-$testcase-iso"
  iso="build/naseed-ash-pipe-$testcase.iso"
  log="build/ash-pipe-$testcase-serial.log"
  clean="build/ash-pipe-$testcase-clean.log"
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
     ! grep -Fxq "$expected" "$clean" ||
     ! grep -Fq '[PIPE] created read=' "$clean" ||
     ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
    echo "[FAIL] real ash $testcase pipeline did not produce $expected" >&2
    tail -110 "$clean" >&2
    exit 1
  fi
  echo "[ACCEPTED] unmodified ash $testcase pipeline output=$expected"
done
