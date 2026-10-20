#!/bin/sh
set -eu
iso="$1"
mkdir -p build
status=0
timeout 12s sh scripts/run.sh "$iso" > build/bash-serial.log 2>&1 || status=$?
echo "===== NaSeed / GNU Bash 5.2.37 real QEMU trace ====="
grep -E '^\[(BOOT|MM|M1|M2|MISS|FAULT|FAIL|EXIT|PASS)\]' build/bash-serial.log | tail -80 || true
if grep -Fq '[PASS] M2 real GNU Bash -c echo hello' build/bash-serial.log; then
    test "$status" -eq 1
    echo "[PROBE] genuine Bash acceptance confirmed"
elif grep -Fq '[M2] real GNU Bash module discovered' build/bash-serial.log; then
    echo "[PROBE] Bash booted; outstanding ABI/kernel blockers remain. NOT A PASS."
else
    cat build/bash-serial.log
    echo "[PROBE] Bash module did not initialize" >&2
    exit 1
fi
