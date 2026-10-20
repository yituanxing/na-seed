#!/bin/sh
# Real unmodified BusyBox ash exercises Linux anonymous pipes and POSIX EOF.
set -eu
for testcase in builtin external chain large; do
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
    large)
      args="sh -c '/bin/busybox cat /bin/busybox | /bin/busybox wc -c'"
      expected=$(wc -c < build/alpine/bin/busybox | tr -d ' ')
      if [ "$expected" -le 4096 ]; then
        echo '[FAIL] fixture smaller than pipe ring; not a blocking test' >&2
        exit 1
      fi ;;
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
  timeout 25s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
  tr -d '\r' < "$log" > "$clean"
  if [ "$rc" -ne 1 ] ||
     ! grep -Fxq "$expected" "$clean" ||
     ! grep -Fq '[PIPE] created read=' "$clean" ||
     ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean"; then
    echo "[FAIL] real ash $testcase pipeline did not produce $expected" >&2
    tail -110 "$clean" >&2
    exit 1
  fi
  if [ "$testcase" = large ]; then
    if ! grep -Fq '[SCHED] blocked pid=' "$clean" ||
       ! grep -Fq '[SCHED] from=' "$clean"; then
      echo '[FAIL] large input completed without real block and task switch' >&2
      tail -90 "$clean" >&2
      exit 1
    fi
  fi
  echo "[ACCEPTED] unmodified ash $testcase pipeline output=$expected"
done
