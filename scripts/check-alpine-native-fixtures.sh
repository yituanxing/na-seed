#!/bin/sh
# Verify every official Alpine ET_DYN package is SHA256 pinned and no
# architecture silently switches versions or unverified discovery mode.
set -eu
manifest=scripts/alpine-native-fixtures.txt
test -s "$manifest"
[ "$(grep -c -v '^#' "$manifest")" -eq 4 ]
awk -F '|' '
    /^[^#]/ {
        if (NF != 3 || ($1 != "aarch64" && $1 != "riscv64"))
            exit 1
        if ($2 != "busybox-1.37.0-r30" && $2 != "musl-1.2.5-r23")
            exit 1
        if (length($3) != 64 || $3 !~ /^[0-9a-f]+$/) exit 1
        if (seen[$1 "|" $2]++) exit 1
        count++
    }
    END { if (count != 4) exit 1 }
' "$manifest"
for target in arm64 riscv64; do
    grep -Fq 'sha256sum -c -' scripts/fetch-alpine-native.sh
    grep -Fq 'scripts/alpine-native-fixtures.txt' scripts/fetch-alpine-native.sh
    grep -Fq 'scripts/probe-alpine-native-dynamic.sh' scripts/probe-alpine-native-matrix.sh
done
for script in scripts/fetch-alpine-native.sh scripts/stage-alpine-native-rootfs.sh \
              scripts/probe-alpine-native-dynamic.sh scripts/probe-alpine-native-matrix.sh; do
    sh -n "$script"
done
echo '[PASS] native ARM64/RV64 Alpine packages pinned with four SHA256 digests'
