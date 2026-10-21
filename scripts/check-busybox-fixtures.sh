#!/bin/sh
# Structural invariants requiring x86 and RV tests to exercise comparable
# BusyBox source versions and one declared static-package family.
set -eu
manifest=scripts/busybox-static-fixtures.txt
test -s "$manifest"
count=$(grep -vc '^#' "$manifest")
test "$count" -eq 3
x86=$(awk -F '|' '$1 == "amd64" {print $2 "|" $3}' "$manifest")
rv=$(awk -F '|' '$1 == "riscv64" {print $2 "|" $3}' "$manifest")
test "$x86" = '1.37.0-6+b9|1.37.0'
test "$x86" = "$rv"
arm=$(awk -F '|' '$1 == "arm64" {print $2 "|" $3}' "$manifest")
test "$x86" = "$arm"
for arch in amd64 riscv64 arm64; do
    awk -F '|' -v arch="$arch" '$1 == arch {
        if (NF != 5 || length($4) != 64 || $5 == "") exit 1
        found++
    } END { if (found != 1) exit 1 }' "$manifest"
done
grep -Fq 'scripts/fetch-busybox-fixture.sh amd64' scripts/fetch-debian.sh
grep -Fq 'scripts/fetch-busybox-fixture.sh riscv64' scripts/fetch-debian-riscv64.sh
grep -Fq 'scripts/fetch-busybox-fixture.sh arm64' scripts/probe-aarch64-busybox-matrix.sh
test "$(grep -vc '^#' scripts/busybox-parity-cases.tsv)" -eq 17
awk -F '\t' '
    /^[^#]/ {
        if (NF != 5 || $1 == "" || ($2 != "direct" && $2 != "cpio"))
            exit 1
        if (used[$1]++) exit 1
    }
' scripts/busybox-parity-cases.tsv
grep -Fq 'scripts/busybox-parity-cases.tsv' scripts/probe-riscv64-busybox-matrix.sh
sh -n scripts/stage-busybox-rootfs.sh
sh -n scripts/probe-riscv64-busybox-matrix.sh
sh -n scripts/probe-x86-busybox-parity.sh
sh -n scripts/probe-aarch64-busybox-matrix.sh
grep -Fq 'scripts/busybox-parity-cases.tsv' scripts/probe-x86-busybox-parity.sh
sh -n scripts/fetch-busybox-fixture.sh
sh -n scripts/fetch-debian-riscv64.sh
sh -n scripts/fetch-debian.sh
echo '[PASS] both ISAs use same pinned Debian static BusyBox source/package'
