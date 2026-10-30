#!/bin/sh
# A green probe means the actual binary reached userspace, NOT that it passed!
set -eu
kind="$1"
iso="$2"
log="build/debian-$kind-serial.log"
status=0
timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || status=$?
clean="build/debian-$kind-clean.log"
tr -d '\r' < "$log" > "$clean"
echo "===== Debian official static $kind, actual NaSeed/QEMU trace ====="
grep -E '^\[(BOOT|MM|M1|M2|M3|MISS|FAULT|FAIL|EXIT|PASS)\]' "$log" | tail -80 || true
if [ "$kind" = bash ]; then
  name='Bash'
else
  name='BusyBox'
fi
if [ "$status" -eq 1 ] &&
   grep -Fxq 'hello' "$clean" &&
   grep -Fq '[ELF] userspace entry=' "$clean" &&
   grep -Fq '[EXIT] user status=0' "$clean" &&
   ! grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
  echo "[ACCEPTED] Debian $name wrote exact stdout hello and exited 0"
elif grep -Fq '[ELF] userspace entry=' "$clean"; then
  echo "[BLOCKED] Debian $kind entered kernel but did not pass"
  echo "[DIAG] Unfiltered tail (includes user stderr):"
  tail -90 "$log"
  if [ "$(printenv DEBIAN_REQUIRE_PASS || echo 0)" = 1 ]; then
    echo "[FAIL] real Debian $kind behavior not accepted" >&2
    exit 1
  fi
else
  cat "$log"
  echo "[FAIL] Debian $kind was not loaded" >&2
  exit 1
fi
