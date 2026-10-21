#!/bin/sh
# Full-kernel SERIAL EVIDENCE for a real PID1 service lifecycle. Reuse
# exactly the same contract on x86-64, RV64 and AArch64; no ISA branches.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: check-m13-init-log.sh SERIAL_LOG' >&2; exit 2; }
log=$1
test -s "$log"
require_one() {
    if ! grep -Fxq "$1" "$log"; then
        echo "[M13 INIT FAIL] missing exact guest marker: $1" >&2
        tail -70 "$log" >&2
        exit 1
    fi
}
require_three() {
    count=$(grep -Fxc "$1" "$log" || :)
    if [ "$count" -ne 3 ]; then
        echo "[M13 INIT FAIL] wanted three guest occurrences [$1], got $count" >&2
        tail -70 "$log" >&2
        exit 1
    fi
}
require_one '[M5] CPIO executable selected: /bin/m13-init'
require_three 'm13-init-round-ok'
require_three 'naseed'
require_one 'm13-init-ok'
# No emulator boot-status marker alone is an acceptable userspace result.
# Prove genuine kernel-side fork, execve and wait4 on the logged path.
grep -Fq '[PROC] fork parent=1 child=' "$log" || exit 1
grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/m12-process' "$log" || exit 1
grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$log" || exit 1
grep -Fq '[PROC] wait4 reaped pid=' "$log" || exit 1
echo '[M13 INIT ACCEPTED] real CPIO PID1 supervised/reaped three process lifecycles'
