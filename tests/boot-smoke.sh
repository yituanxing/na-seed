#!/bin/sh
# Only the fundamental serial boot contract; no M-number test forest.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 <iso>" >&2; exit 2; }
log=$(mktemp)
trap 'rm -f "$log"' 0
trap 'exit 2' 1 2 3 15
rc=0
timeout 45s sh scripts/run.sh "$1" >"$log" 2>&1 || rc=$?
if [ "$rc" -ne 1 ]; then
    cat "$log"
    echo "FAIL: QEMU returned $rc (expected isa-debug-exit=1)" >&2
    exit 1
fi
# A clean QEMU debug-exit alone is not sufficient evidence of user mode.
# Require the real boot path, cooperative kernel continuations, an actual
# ring-3 ELF's syscall output, and final PID1 exit/teardown.
for marker in \
    '[BOOT] NaSeed kernel entered' \
    '[PASS] memory allocator' \
    '[PASS] boot / serial / memory' \
    '[M91 KCTX PASS]' \
    '[M92 KWAIT PASS]' \
    '[M95 KTHREAD PASS]' \
    '[USER] hello from standalone ELF via syscall' \
    '[PASS] userspace exit 0'; do
    if ! grep -Fq "$marker" "$log"; then
        cat "$log"
        echo "FAIL: missing serial marker: $marker" >&2
        exit 1
    fi
done
echo 'PASS: x86_64 UEFI, native scheduler, user ELF/syscalls, PID1 exit'
