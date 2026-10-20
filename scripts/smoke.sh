#!/bin/sh
set -eu
iso="$1"
mkdir -p build
rc=0
timeout 35s sh scripts/run.sh "$iso" > build/serial.log 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
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
