#!/bin/sh
# Nine different signed Alpine LLVM21/Clang ELFs, one x86 NaSeed QEMU boot.
# Run each preexisting semantic probe even after a failure, so the serial log
# records a complete compatibility matrix instead of stopping at one tool.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL

failed=0
for entry in \
    llvm-tblgen:TBLGEN \
    llvm-remarkutil:REMARKUTIL \
    llvm-rc:RC \
    llvm-reduce:REDUCE \
    llvm-lto:LTO \
    llvm-ar:AR \
    llvm-strip:STRIP \
    llvm-addr2line:ADDR2LINE \
    clang-object:CLANG
do
    case_name=${entry%%:*}
    symbol=${entry#*:}
    if /bin/sh /tests/m141-toolchain-run.sh "$case_name"; then
        /bin/busybox printf 'M142_SIGNED_LLVM_%s_PASS\n' "$symbol"
    else
        /bin/busybox printf '[M142 LLVM COHORT FAIL] %s\n' "$case_name"
        failed=1
    fi
done

[ "$failed" -eq 0 ] || exit 1
/bin/busybox printf 'M142_SIGNED_LLVM_COHORT_PASS\n'
