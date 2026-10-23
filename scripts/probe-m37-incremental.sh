#!/bin/sh
# All three architectures, same unchanged GNU Make, original zlib sources,
# genuine Linux stat/utimensat and exact dependency-driven incremental build.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
    amd64) runner=scripts/probe-x86-busybox-parity.sh
      dir=build/busybox-source-parity-x86 ;;
    riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh
      dir=build/port-riscv64/full/matrix-source ;;
    arm64) runner=scripts/probe-aarch64-busybox-matrix.sh
      dir=build/port-aarch64/full/matrix-source ;;
    *) exit 2 ;;
esac
port=$1
work="build/external/m36-zlib/$port"
test -s "$work/rootfs.cpio"
export NAS_ZLIB_MAKEFILE=tests/m37-incremental.mk
export NAS_ZLIB_INIT_SOURCE=tests/m37-native-init.c
sh scripts/stage-m36-zlib-rootfs.sh "$port"
export BUSYBOX_PARITY_BINARY="build/external/m34-gnumake/$port/gmake"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m37-incremental-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
sh "$runner"
log="$dir/m37-incremental.clean.log"
test -s "$log"
# 12 initial object compilations + precisely one rewritten dependency.
num=$(grep -Fc M37_OBJECT: "$log" || :)
deflate=$(grep -Fxc M37_OBJECT:deflate.o "$log" || :)
link=$(grep -Fxc M37_LINK "$log" || :)
test "$num" -eq 13 && test "$deflate" -eq 2 && test "$link" -eq 2 || {
    echo "[M37 FAIL] wrong incremental object count=$num deflate=$deflate link=$link" >&2
    tail -120 "$log" >&2
    exit 1
}
grep -Fxq M37_UP_TO_DATE "$log"
grep -Fxq M37_DIRTY_DEPENDENCY "$log"
grep -Fxq M37_INCREMENTAL_COMPLETE "$log"
echo "[M37 INCREMENTAL PASS] $port real GNU Make -j2 + stat + utimensat + 1/12 selective rebuild"
