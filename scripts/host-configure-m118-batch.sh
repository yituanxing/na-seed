#!/bin/sh
# All clang/LLD work is on the Linux host; NaSeed only loads completed ELF.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in
    amd64) triple=x86_64-linux-musl; emulation=elf_x86_64 ;;
    arm64) triple=aarch64-linux-musl; emulation=aarch64linux ;;
    riscv64) triple=riscv64-linux-musl; emulation=elf64lriscv ;;
    *) exit 2 ;;
esac
command -v clang >/dev/null
command -v ld.lld >/dev/null
command -v readelf >/dev/null
mkdir -p "$root/tests"
for source in m118-debug m118-debug-other; do
    clang --target="$triple" -g -gdwarf-4 -O0 -ffreestanding \
        -fno-stack-protector -c "tests/$source.c" -o "$root/tests/$source.o"
done
# eu-unstrip reconstructs a linked ELF's allocated and debug sections.  The
# relocatable .o exercises eu-strip separately, but its .rela.debug_* sections
# refer to .symtab and are not a valid eu-unstrip pair on all elfutils ports.
ld.lld -m "$emulation" -e m118_answer \
    "$root/tests/m118-debug.o" -o "$root/tests/m118-debug.elf"
readelf -h "$root/tests/m118-debug.elf" | grep -Eq 'EXEC \(Executable file\)' || {
    echo "[M118 HOST FIXTURE FAIL] $port linked executable missing" >&2
    exit 1
}
readelf -S "$root/tests/m118-debug.elf" | grep -Fq '.debug_info' || {
    echo "[M118 HOST FIXTURE FAIL] $port linked DWARF missing" >&2
    exit 1
}
clang --target="$triple" -c tests/m118-textrel.s \
    -o "$root/tests/m118-textrel.o"
ld.lld -m "$emulation" -shared -z notext \
    "$root/tests/m118-textrel.o" -o "$root/tests/m118-textrel.so"
readelf -d "$root/tests/m118-textrel.so" | grep -Eq 'TEXTREL' || {
    echo "[M118 HOST FIXTURE FAIL] $port lacks genuine DT_TEXTREL" >&2
    exit 1
}
for object in m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so; do
    test -s "$root/tests/$object"
done
echo "[M118 HOST FIXTURES PASS] $port native DWARF and real text relocation, zero guest builds"
