#!/bin/sh
# Host/static only: the real original iostat ELF must run in three actual
# NaSeed QEMU guests, where probe-m174-batch.sh strictly checks this marker.
set -eu
sh scripts/check-m184-software-functions.sh
sh -n tests/m174-software-run.sh
test "$(grep -Fxc 'M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS' \
    scripts/m174-expected-markers.txt)" -eq 1
test "$(grep -Fxc 'echo M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS' \
    tests/m174-software-run.sh)" -eq 1
grep -Fq 'busy timeout 15 /bin/iostat -c 1 2' tests/m174-software-run.sh
grep -Fq 'reports!=2 || samples!=2' tests/m174-software-run.sh
grep -Fxq 'bin/iostat' scripts/m174-software-original-elfs.txt
grep -Fq 'tests/m174-software-run.sh' scripts/m174-software-rootfs-copies.tsv
echo '[M185 SOFTWARE HOST CONTRACT PASS] 20 signed Alpine ELFs unchanged; original iostat real-CPU two-sample guest oracle staged'
