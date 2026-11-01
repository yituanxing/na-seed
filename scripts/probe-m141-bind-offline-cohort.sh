#!/bin/sh
# Run five different unmodified, signed Alpine BIND9 executables in ONE x86
# QEMU guest boot. Keep semantic oracles inside the established M141 DNS
# fixture runner; this host gate only checks the independent results.
set -eu
[ "$#" -eq 0 ] || {
    echo 'usage: sh scripts/probe-m141-bind-offline-cohort.sh' >&2; exit 2;
}
export NAS_APP_BATCH_QEMU_TIMEOUT=180
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m141-bind-offline-cohort-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m141-original-signed-elfs.sha256"
test -s "$root/rootfs/bin/busybox"
for exe in usr/bin/arpaname usr/bin/named-rrchecker \
           usr/bin/named-checkzone usr/sbin/rndc-confgen \
           usr/sbin/ddns-confgen; do
    test -s "$root/rootfs/$exe"
    grep -Fxq "$exe" scripts/m141-original-elfs.txt || {
        echo "[M141 BIND OFFLINE FAIL] unsigned/unlisted $exe" >&2; exit 1;
    }
done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
for pair in \
    'ARPA:usr/bin/arpaname' \
    'RR:usr/bin/named-rrchecker' \
    'ZONE:usr/bin/named-checkzone' \
    'RNDC_KEY:usr/sbin/rndc-confgen' \
    'DDNS_KEY:usr/sbin/ddns-confgen'; do
    marker=${pair%%:*}
    elf=${pair#*:}
    grep -Fxq "M141_SYSTEM_OFFLINE_${marker}_PASS" "$clean" || {
        echo "[M141 BIND OFFLINE FAIL] $marker semantic test did not pass" >&2;
        exit 1;
    }
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
        echo "[M141 BIND OFFLINE FAIL] original Alpine ELF /$elf did not exec" >&2;
        exit 1;
    }
done
grep -Fxq M141_SYSTEM_BIND_OFFLINE_COHORT_PASS "$clean" || {
    echo '[M141 BIND OFFLINE FAIL] missing all-five cohort marker' >&2;
    exit 1;
}
echo '[M141 BIND OFFLINE QEMU PASS] 5 distinct signed Alpine BIND9 ELFs: arpaname/rrchecker/checkzone/rndc-confgen/ddns-confgen; one x86 guest boot'
