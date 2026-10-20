#!/bin/sh
# A green probe means the actual binary reached userspace, NOT that it passed!
set -eu
kind="$1"
iso="$2"
log="build/debian-$kind-serial.log"
status=0
timeout 12s sh scripts/run.sh "$iso" > "$log" 2>&1 || status=$?
echo "===== Debian official static $kind, actual NaSeed/QEMU trace ====="
grep -E '^\[(BOOT|MM|M1|M2|M3|MISS|FAULT|FAIL|EXIT|PASS)\]' "$log" | tail -80 || true
if [ "$kind" = bash ]; then
  pass='[PASS] M2 real GNU Bash -c echo hello'
  loaded='[M2] real GNU Bash module discovered'
else
  pass='[PASS] M2 Debian BusyBox echo hello'
  loaded='[M3] real BusyBox module discovered'
fi
if grep -Fq "$pass" "$log"; then
  if [ "$status" -ne 1 ]; then
    echo "[FAIL] printed pass without QEMU debug-exit(0)" >&2
    exit 1
  fi
  echo "[ACCEPTED] Debian $kind wrote hello and exited 0"
elif grep -Fq "$loaded" "$log"; then
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
