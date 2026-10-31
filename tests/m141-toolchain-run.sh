#!/bin/sh
# M141: independent original Alpine LLVM21/Clang toolchain semantic probes.
# Invoked as: sh /tests/m141-toolchain-run.sh CASE
# No PASS marker is emitted here; the outer batch runner owns its oracle.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
}

test "$#" -eq 1 || exit 2
case_name=$1
case "$case_name" in
    clang-object|clang-assembly|llvm-ar|llvm-strip|llvm-addr2line|\
    llvm-dwarfutil|llvm-dwp|llvm-lto|llvm-tblgen|llvm-remarkutil|\
    llvm-rc|llvm-reduce) ;;
    *) echo "[M141 TOOLCHAIN UNKNOWN CASE] $case_name" >&2; exit 2 ;;
esac

work="/tmp/m141-toolchain-$case_name"
busy mkdir -p "$work"
# Keep a bounded diagnostic from the failing native ELF in the QEMU serial.
# The ordinary PASS oracle stays silent; details are useful only on failure.
toolchain_failure() {
    code=$?
    if [ "$code" -ne 0 ]; then
        busy printf '[M141 TOOLCHAIN DIAG] %s exit=%s\n' "$case_name" "$code"
        for file in "$work"/dwarfutil.err "$work"/verify "$work"/reduce.log; do
            if [ -s "$file" ]; then
                busy printf '[M141 TOOLCHAIN DIAG] %s\n' "$file"
                busy head -n 12 "$file"
            fi
        done
    fi
}
trap toolchain_failure EXIT
busy printf '%s\n' \
    'int m141_double(int x) {' \
    '  return x * 2;' \
    '}' > "$work/m141.c"
busy printf '%s\n' \
    'int m141_triple(int x) {' \
    '  return x * 3;' \
    '}' > "$work/m141_other.c"

compile_object() {
    original_elf /usr/bin/clang
    /usr/bin/clang -g -O0 -c "$work/m141.c" -o "$work/m141.o"
    [ -s "$work/m141.o" ]
}

object_magic() {
    magic=$(busy dd if="$1" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ]
}

seed_ir() {
    busy printf '%s\n' \
        'define i32 @m141_ir_add(i32 %x) {' \
        'entry:' \
        '  %y = add i32 %x, 13' \
        '  ret i32 %y' \
        '}' > "$work/m141.ll"
}

case "$case_name" in
    clang-object)
        compile_object
        object_magic "$work/m141.o"
        /usr/bin/llvm21-nm "$work/m141.o" > "$work/nm.txt"
        busy grep -Fq m141_double "$work/nm.txt" ;;

    clang-assembly)
        original_elf /usr/bin/clang
        /usr/bin/clang -O1 -S "$work/m141.c" -o "$work/m141.s"
        [ -s "$work/m141.s" ]
        busy grep -Fq m141_double "$work/m141.s"
        /usr/bin/clang -c "$work/m141.s" -o "$work/from-asm.o"
        object_magic "$work/from-asm.o" ;;

    llvm-ar)
        original_elf /usr/bin/llvm21-ar
        compile_object
        /usr/bin/clang -c "$work/m141_other.c" -o "$work/other.o"
        /usr/bin/llvm21-ar rcs "$work/libm141.a" "$work/m141.o" "$work/other.o"
        /usr/bin/llvm21-ar t "$work/libm141.a" > "$work/members"
        busy grep -Fqx m141.o "$work/members"
        busy grep -Fqx other.o "$work/members"
        /usr/bin/llvm21-ar p "$work/libm141.a" m141.o > "$work/extracted.o"
        busy cmp -s "$work/m141.o" "$work/extracted.o" ;;

    llvm-strip)
        original_elf /usr/bin/llvm21-strip
        compile_object
        /usr/bin/llvm21-strip --strip-debug -o "$work/stripped.o" "$work/m141.o"
        object_magic "$work/stripped.o"
        before=$(busy wc -c < "$work/m141.o")
        after=$(busy wc -c < "$work/stripped.o")
        [ "$after" -lt "$before" ]
        /usr/bin/llvm21-nm "$work/stripped.o" > "$work/nm.txt"
        busy grep -Fq m141_double "$work/nm.txt" ;;

    llvm-addr2line)
        original_elf /usr/bin/llvm21-addr2line
        compile_object
        address=$(/usr/bin/llvm21-nm "$work/m141.o" |
                  busy awk '$3 == "m141_double" {print $1; exit}')
        [ -n "$address" ]
        /usr/bin/llvm21-addr2line -e "$work/m141.o" "0x$address" \
            > "$work/location"
        busy grep -Fq m141.c "$work/location"
        ! busy grep -Fq '??:0' "$work/location" ;;

    llvm-dwarfutil)
        original_elf /usr/bin/llvm21-dwarfutil
        # The DWARF editor resolves address/range relocations best on a
        # linked ELF, unlike llvm-strip's relocatable object fixture.
        busy printf '%s\n' \
            'int m141_double(int);' \
            'int main(void) { return m141_double(2) == 4 ? 0 : 1; }' \
            > "$work/link.c"
        /usr/bin/clang -g -O0 "$work/m141.c" "$work/link.c" \
            -o "$work/m141.elf"
        object_magic "$work/m141.elf"
        /usr/bin/llvm21-dwarfutil "$work/m141.elf" "$work/copied.elf" \
            2> "$work/dwarfutil.err"
        object_magic "$work/copied.elf"
        /usr/bin/llvm21-dwarfdump --verify "$work/copied.elf" \
            > "$work/verify" 2>&1
        /usr/bin/llvm21-dwarfdump --debug-info "$work/copied.elf" \
            > "$work/dwarf"
        busy grep -Fq m141.c "$work/dwarf" ;;

    llvm-dwp)
        original_elf /usr/bin/llvm21-dwp
        # RISC-V Clang rejects split DWARF with its default relaxation;
        # -mno-relax is also accepted on the x86-64/AArch64 cross cases
        # only when the flag is specific to the RISC-V target.
        split_flags=
        case "$(/usr/bin/clang -dumpmachine)" in
            riscv64*) split_flags=-mno-relax ;;
        esac
        /usr/bin/clang $split_flags -gsplit-dwarf -g -O0 -c "$work/m141.c" \
            -o "$work/split.o"
        [ -s "$work/split.dwo" ]
        /usr/bin/llvm21-dwp "$work/split.dwo" -o "$work/merged.dwp"
        [ -s "$work/merged.dwp" ]
        /usr/bin/llvm21-dwarfdump --verify "$work/merged.dwp" \
            > "$work/verify" 2>&1 ;;

    llvm-lto)
        original_elf /usr/bin/llvm21-lto
        /usr/bin/clang -flto -O0 -c "$work/m141.c" \
            -o "$work/m141-bitcode.o"
        /usr/bin/llvm21-lto -list-symbols-only "$work/m141-bitcode.o" \
            > "$work/symbols"
        busy grep -Fq m141_double "$work/symbols" ;;

    llvm-tblgen)
        original_elf /usr/bin/llvm21-tblgen
        busy printf '%s\n' \
            'class M141Item<string s> {' \
            '  string Value = s;' \
            '}' \
            'def M141Record : M141Item<"original-elf">;' \
            > "$work/m141.td"
        /usr/bin/llvm21-tblgen -print-records "$work/m141.td" \
            > "$work/records"
        busy grep -Fq 'def M141Record' "$work/records"
        busy grep -Fq original-elf "$work/records" ;;

    llvm-remarkutil)
        original_elf /usr/bin/llvm21-remarkutil
        busy printf '%s\n' \
            '--- !Passed' \
            'Pass: inline' \
            'Name: Inlined' \
            'DebugLoc: { File: m141.c, Line: 1, Column: 1 }' \
            'Function: m141_double' \
            'Args:' \
            '  - String: inlined' \
            '...' > "$work/remark.yaml"
        /usr/bin/llvm21-remarkutil yaml2bitstream "$work/remark.yaml" \
            -o "$work/remark.bitstream"
        [ -s "$work/remark.bitstream" ]
        /usr/bin/llvm21-remarkutil bitstream2yaml "$work/remark.bitstream" \
            -o "$work/roundtrip.yaml"
        # The YAML serializer is free to quote string values.  Assert the
        # semantic identity of the remark, not its incidental whitespace or
        # scalar quoting style.
        busy grep -Fq m141_double "$work/roundtrip.yaml"
        busy grep -Fq Inlined "$work/roundtrip.yaml"
        busy grep -Fq inline "$work/roundtrip.yaml" ;;

    llvm-rc)
        original_elf /usr/bin/llvm21-rc
        # No Windows SDK is required by the resource itself.
        busy printf '%s\n' '1 RCDATA { "M141-ORIGINAL" }' \
            > "$work/m141.rc"
        /usr/bin/llvm21-rc /FO "$work/m141.res" "$work/m141.rc"
        [ -s "$work/m141.res" ]
        busy grep -aq M141-ORIGINAL "$work/m141.res" ;;

    llvm-reduce)
        original_elf /usr/bin/llvm21-reduce
        seed_ir
        # llvm-reduce executes --test via execve.  /bin/true is not a required
        # binary in the signed BusyBox root.  Invoke the guaranteed real ELF
        # and keep the IR function instead of using an always-true predicate.
        /usr/bin/llvm21-reduce --test=/bin/busybox \
            --test-arg=grep --test-arg=-Fq --test-arg=m141_ir_add \
            "$work/m141.ll" \
            -o "$work/reduced.ll" > "$work/reduce.log" 2>&1
        busy grep -Fq m141_ir_add "$work/reduced.ll"
        /usr/bin/llvm21-as "$work/reduced.ll" -o "$work/reduced.bc"
        [ -s "$work/reduced.bc" ] ;;
esac
