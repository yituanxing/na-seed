#!/bin/sh
set -eu
iso="$1"
mkdir -p build
rc=0
timeout 35s sh scripts/run.sh "$iso" > build/serial.log 2>&1 || rc=$?
if [ "$rc" -ne 1 ]; then
    cat build/serial.log
    exit 1
fi
if ! grep -Fq '[PASS] M0 memory allocator' build/serial.log ||
   ! grep -Fq '[PASS] M0 boot / serial / memory' build/serial.log; then
    cat build/serial.log
    echo "M0 serial acceptance markers are missing" >&2
    exit 1
fi
grep -F '[PASS] M0' build/serial.log
if ! grep -Fq '[USER] hello from standalone ELF via syscall' build/serial.log ||
   ! grep -Fq '[MISS] syscall nr=999' build/serial.log ||
   ! grep -Fq '[EXIT] user status=0' build/serial.log ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' build/serial.log; then
    cat build/serial.log
    echo 'M1 user-space acceptance markers missing' >&2
    exit 1
fi
echo '[M1 ACCEPTED] genuine user ELF wrote, saw ENOSYS, exited 0'
