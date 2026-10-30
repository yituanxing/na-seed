#!/bin/sh
set -eu
iso="$1"
mkdir -p build
status=0
timeout 12s sh scripts/run.sh "$iso" > build/bash-serial.log 2>&1 || status=$?
tr -d '\r' < build/bash-serial.log > build/bash-clean.log
echo "===== NaSeed / GNU Bash 5.2.37 real QEMU trace ====="
grep -E '^\[(BOOT|MM|M1|M2|MISS|FAULT|FAIL|EXIT|PASS)\]' build/bash-serial.log | tail -80 || true
# Resolve real fault RIP against the unstripped musl Bash ELF.
# This distinguishes an unsupported ISA opcode from an intentional UD2 trap.
rip=$(sed -n 's/.*\[FAULT\].*rip=\(0x[0-9a-fA-F]*\).*/\1/p' build/bash-serial.log | head -1)
if [ -n "$rip" ]; then
    echo "[DIAG] Bash user RIP $rip"
    addr2line -e build/userspace/bash -f "$rip" || true
    lo=$(printf '0x%x' "$((rip - 48))")
    hi=$(printf '0x%x' "$((rip + 80))")
    objdump -d --start-address="$lo" --stop-address="$hi" \
        build/userspace/bash | tail -90 || true
fi
if [ "$status" -eq 1 ] &&
   grep -Fxq 'hello' build/bash-clean.log &&
   grep -Fq '[ELF] userspace entry=' build/bash-clean.log &&
   grep -Fq '[EXIT] user status=0' build/bash-clean.log &&
   ! grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' build/bash-clean.log; then
    echo "[PROBE] genuine Bash acceptance confirmed"
elif grep -Fq '[ELF] userspace entry=' build/bash-clean.log; then
    echo "[PROBE] Bash booted; outstanding ABI/kernel blockers remain. NOT A PASS."
    if [ "${BASH_REQUIRE_PASS:-0}" = "1" ]; then
        echo "[FAIL] required real Bash stdout and zero exit were not observed" >&2
        exit 1
    fi
else
    cat build/bash-serial.log
    echo "[PROBE] Bash module did not initialize" >&2
    exit 1
fi
