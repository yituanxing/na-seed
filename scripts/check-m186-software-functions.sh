#!/bin/sh
# Host-only contract; actual original pidstat must execute under each QEMU
# native ISA and pass the guest's unique marker oracle.
set -eu
sh scripts/check-m185-software-functions.sh
grep -Fq 'busy timeout 15 /usr/bin/pidstat -u -p SELF 1 2' tests/m174-software-run.sh
grep -Fq 'echo M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' tests/m174-software-run.sh
grep -Fxq 'M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' scripts/m174-expected-markers.txt
grep -Fxq 'usr/bin/pidstat' scripts/m174-software-original-elfs.txt
if [ "$(grep -Fxc 'M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' scripts/m174-expected-markers.txt)" -ne 1 ]; then
    echo '[M186 SOFTWARE HOST FAIL] PIDSTAT marker not unique' >&2
    exit 1
fi
echo '[M186 SOFTWARE HOST CONTRACT PASS] 20 original signed ELFs unchanged; real per-task pidstat CPU sampling staged'
