#!/bin/sh
# Exercise original, unchanged Alpine LLVM21 and Clang ELFs on NaSeed.
# Compile/parse/strip/archive semantics are checked in the guest, not faked
# by a host compiler or a synthetic stdout fixture.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-m142-llvm-cohort.sh' >&2; exit 2; }
export NAS_APP_BATCH_QEMU_TIMEOUT=300
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m142-llvm-cohort-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m141-original-signed-elfs.sha256"
test -s "$root/rootfs/bin/busybox"
test -s "$root/rootfs/tests/m142-llvm-cohort-run.sh"
for elf in \
    usr/bin/llvm21-tblgen usr/bin/llvm21-remarkutil usr/bin/llvm21-rc \
    usr/bin/llvm21-reduce usr/bin/llvm21-lto usr/bin/llvm21-ar \
    usr/bin/llvm21-strip usr/bin/llvm21-addr2line usr/bin/clang; do
    test -s "$root/rootfs/$elf" || {
        echo "[M142 LLVM COHORT FAIL] missing signed ELF: $elf" >&2; exit 1;
    }
    grep -Fxq "$elf" scripts/m141-original-elfs.txt || {
        echo "[M142 LLVM COHORT FAIL] unsigned/unlisted executable: $elf" >&2;
        exit 1;
    }
done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
for spec in \
    TBLGEN:usr/bin/llvm21-tblgen \
    REMARKUTIL:usr/bin/llvm21-remarkutil \
    RC:usr/bin/llvm21-rc \
    REDUCE:usr/bin/llvm21-reduce \
    LTO:usr/bin/llvm21-lto \
    AR:usr/bin/llvm21-ar \
    STRIP:usr/bin/llvm21-strip \
    ADDR2LINE:usr/bin/llvm21-addr2line \
    CLANG:usr/bin/clang; do
    marker=${spec%%:*}
    elf=${spec#*:}
    grep -Fxq "M142_SIGNED_LLVM_${marker}_PASS" "$clean" || {
        echo "[M142 LLVM COHORT FAIL] missing original $marker semantic PASS" >&2;
        exit 1;
    }
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
        echo "[M142 LLVM COHORT FAIL] no actual guest exec for $elf" >&2;
        exit 1;
    }
done
grep -Fxq M142_SIGNED_LLVM_COHORT_PASS "$clean" || {
    echo '[M142 LLVM COHORT FAIL] missing all-nine summary' >&2; exit 1;
}
echo '[M142 LLVM COHORT QEMU PASS] nine different original signed Alpine LLVM21/Clang ELFs, real parse/compile/archive/strip operations'
