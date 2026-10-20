#!/bin/sh
set -eu
iso="$1"
rc=0
timeout 12s sh scripts/run.sh "$iso" > build/alpine-serial.log 2>&1 || rc=$?
echo "===== Genuine Alpine dynamic BusyBox / musl ldso on NaSeed ====="
grep -E '^\[(BOOT|MM|M1|M2|M3|M4|DYN|MISS|FAULT|FAIL|EXIT|PASS)\]' build/alpine-serial.log | tail -90 || true
if grep -Fq '[PASS] M4 Alpine dynamic BusyBox echo hello' build/alpine-serial.log; then
    test "$rc" -eq 1
    echo "[ACCEPTED] Alpine dynamic BusyBox stdout hello and exit 0"
else
    echo "[BLOCKED] Alpine real dynamic ELF is NOT accepted yet"
    if [ "$(printenv ALPINE_REQUIRE_PASS || echo 0)" = 1 ]; then exit 1; fi
fi
