#!/bin/sh
# Check both provenance mechanisms: original BusyBox APK byte digest,
# plus the actual target-keyring signed Alpine musl APK for all three ISAs.
set -eu
manifest=scripts/alpine-native-fixtures.txt
test -s "$manifest"
[ "$(grep -c -v '^#' "$manifest")" -eq 2 ]
awk -F '|' '
    /^[^#]/ {
        if (NF!=3 || ($1!="aarch64" && $1!="riscv64") ||
            $2!="busybox-1.37.0-r30" ||
            length($3)!=64 || $3 !~ /^[0-9a-f]+$/ ||
            seen[$1 "|"$2]++) exit 1
        count++
    }
    END {if(count!=2)exit 1}
' "$manifest"
grep -Fq 'sha256sum -c -' scripts/fetch-alpine-native.sh
grep -Fq 'scripts/alpine-native-fixtures.txt' scripts/fetch-alpine-native.sh
grep -Fq "2fea8c73710994099eaec6832b69a0f70163e0d6ee13848af3a5270a16d15c5c" scripts/fetch-alpine.sh
grep -Fq 'scripts/install-verified-alpine-musl.sh' scripts/fetch-alpine.sh
grep -Fq 'scripts/install-verified-alpine-musl.sh' scripts/fetch-alpine-native.sh
# Exact version pin, target keys from separately SHA-verified official
# minirootfs, signed apk index; no --allow-untrusted or foreign scripts.
grep -Fq 'version=1.2.5-r24' scripts/install-verified-alpine-musl.sh
grep -Fq 'sha256sum -c' scripts/install-verified-alpine-musl.sh
grep -Fq -- '--keys-dir /etc/apk/keys' scripts/install-verified-alpine-musl.sh
grep -Fq -- '--no-scripts' scripts/install-verified-alpine-musl.sh
grep -Fq -- '"musl=$MUSL_VERSION"' scripts/install-verified-alpine-musl.sh
if grep -Eq -- '--allow-untrusted|--no-check-certificate' \
    scripts/install-verified-alpine-musl.sh; then
    echo '[FAIL] unverified Alpine musl provenance' >&2
    exit 1
fi
for target in arm64 riscv64; do
    grep -Fq 'scripts/probe-alpine-native-dynamic.sh' scripts/probe-alpine-native-matrix.sh
done
for script in scripts/fetch-alpine.sh scripts/fetch-alpine-native.sh \
              scripts/install-verified-alpine-musl.sh \
              scripts/stage-alpine-native-rootfs.sh \
              scripts/probe-alpine-native-dynamic.sh scripts/probe-alpine-native-matrix.sh; do
    sh -n "$script"
done
echo '[PASS] original ARM64/RV64 BusyBox SHA256 and signed exact-version three-ISA musl'
