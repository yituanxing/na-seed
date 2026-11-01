#!/bin/sh
# Host-only cross-target DWARF fixture for original Alpine ELF analyzers.
# No ELF is built or modified inside the NaSeed guest.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in
    amd64) triple=x86_64-alpine-linux-musl ;;
    riscv64) triple=riscv64-alpine-linux-musl ;;
    arm64) triple=aarch64-alpine-linux-musl ;;
    *) exit 2 ;;
esac
case "$root" in build/external/m153-batch/*/rootfs) ;; *) exit 2 ;; esac
command -v clang >/dev/null || {
    echo '[M153 FIXTURE FAIL] host clang cross-compiler required' >&2
    exit 1
}
mkdir -p "$root/tests"
for name in fixture fixture-v2 style; do
    cp "tests/m153-$name.c" "$root/tests/m153-$name.c"
done
cp tests/m153-rule.yar "$root/tests/m153-rule.yar"
cp tests/m153-assembly.asm "$root/tests/m153-assembly.asm"
clang --target="$triple" -gdwarf-4 -O0 -fno-inline -ffreestanding -fPIC \
    -c "$root/tests/m153-fixture.c" -o "$root/tests/m153-native.o"
clang --target="$triple" -gdwarf-4 -O0 -fno-inline -ffreestanding -fPIC \
    -c "$root/tests/m153-fixture-v2.c" -o "$root/tests/m153-native-v2.o"
readelf -h "$root/tests/m153-native.o" | grep -Fq ELF
readelf -Ws "$root/tests/m153-native.o" | grep -Fq m153_add
printf '%s\n' 'original ELF diagnostics' 'M153_C_MARKER' \
    > "$root/tests/m153-text.txt"
printf '%s\n' 'original ELF diagnostics' 'M153_D_MARKER' \
    > "$root/tests/m153-text-v2.txt"
echo "[M153 HOST FIXTURE PASS] $port target ISA $triple, two distinct DWARF ELFs and real source"
