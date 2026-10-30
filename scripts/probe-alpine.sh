#!/bin/sh
set -eu
iso="$1"
rc=0
timeout 12s sh scripts/run.sh "$iso" > build/alpine-serial.log 2>&1 || rc=$?
tr -d '\r' < build/alpine-serial.log > build/alpine-clean.log
echo "===== Genuine Alpine dynamic BusyBox / musl ldso on NaSeed ====="
grep -E '^\[(BOOT|MM|M1|M2|M3|M4|DYN|MISS|FAULT|FAIL|EXIT|PASS)\]' build/alpine-serial.log | tail -90 || true
if [ "$rc" -eq 1 ] &&
   grep -Fxq 'hello' build/alpine-clean.log &&
   grep -Fq '[DYN] real musl interpreter loaded' build/alpine-clean.log &&
   grep -Fq '[EXIT] user status=0' build/alpine-clean.log &&
   ! grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' build/alpine-clean.log; then
    echo "[ACCEPTED] Alpine dynamic BusyBox stdout hello and exit 0"
else
    echo "[BLOCKED] Alpine real dynamic ELF is NOT accepted yet"
    if [ "$(printenv ALPINE_REQUIRE_PASS || echo 0)" = 1 ]; then exit 1; fi
fi
