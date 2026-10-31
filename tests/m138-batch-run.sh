#!/bin/sh
# M138: twenty distinct unchanged Alpine LLVM 21 ELF tools, real IR and object
# transformations. Every case runs separately and contributes one exact marker.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LANG=C
LC_ALL=C
export PATH HOME LANG LC_ALL
busy() { /bin/busybox "$@"; }

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$candidate"
}

seed_ir() {
    busy printf '%s\n' \
        'source_filename = "m138.ll"' \
        '@m138_message = private unnamed_addr constant [10 x i8] c"M138-LLVM\00"' \
        'define i32 @m138_add(i32 %x) {' \
        'entry:' '  %sum = add i32 %x, 7' '  ret i32 %sum' '}' \
        'define i32 @m138_identity(i32 %x) {' \
        'entry:' '  %sum = add i32 %x, 0' '  ret i32 %sum' '}' \
        'define ptr @m138_text() {' \
        'entry:' '  ret ptr @m138_message' '}' > "$work/source.ll"
    busy printf '%s\n' \
        'define i32 @m138_other(i32 %x) {' \
        'entry:' '  %result = mul i32 %x, 3' '  ret i32 %result' '}' \
        > "$work/other.ll"
}

seed_bc() {
    seed_ir
    /usr/bin/llvm21-as "$work/source.ll" -o "$work/source.bc"
    [ -s "$work/source.bc" ]
}

seed_obj() {
    seed_ir
    /usr/bin/llc21 -mtriple=x86_64-unknown-linux-gnu -filetype=obj \
        "$work/source.ll" -o "$work/source.o"
    [ -s "$work/source.o" ]
}

seed_asm() {
    busy printf '%s\n' \
        '.file 1 "m138.c"' '.text' '.globl m138_mcsymbol' \
        '.type m138_mcsymbol,@function' 'm138_mcsymbol:' \
        '.loc 1 1 0' 'nop' 'ret' > "$work/source.s"
}

run_case() {
    app=$1
    work="/tmp/m138-$app"
    busy mkdir -p "$work"
    case "$app" in
      as)
        exe=$(original_elf /usr/bin/llvm21-as)
        seed_ir
        "$exe" "$work/source.ll" -o "$work/result.bc" > "$work/output" 2>&1
        [ -s "$work/result.bc" ]
        magic=$(busy dd if="$work/result.bc" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 4243c0de ] ;;
      dis)
        exe=$(original_elf /usr/bin/llvm21-dis)
        seed_bc
        "$exe" "$work/source.bc" -o "$work/result.ll" > "$work/output" 2>&1
        busy grep -Fq 'define i32 @m138_add' "$work/result.ll"
        busy grep -Fq 'add i32 %x, 7' "$work/result.ll" ;;
      opt)
        exe=$(original_elf /usr/bin/opt21)
        seed_ir
        "$exe" -passes=instcombine -S "$work/source.ll" -o "$work/result.ll" \
            > "$work/output" 2>&1
        busy grep -Fq 'define i32 @m138_identity' "$work/result.ll"
        ! busy grep -Fq 'add i32 %x, 0' "$work/result.ll"
        busy grep -Fq 'add i32 %x, 7' "$work/result.ll" ;;
      llc)
        exe=$(original_elf /usr/bin/llc21)
        seed_ir
        "$exe" -mtriple=x86_64-unknown-linux-gnu -filetype=obj \
            "$work/source.ll" -o "$work/result.o" > "$work/output" 2>&1
        magic=$(busy dd if="$work/result.o" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 7f454c46 ]
        /usr/bin/llvm21-nm "$work/result.o" > "$work/symbols"
        busy grep -Fq m138_add "$work/symbols" ;;
      link)
        exe=$(original_elf /usr/bin/llvm21-link)
        seed_bc
        /usr/bin/llvm21-as "$work/other.ll" -o "$work/other.bc"
        "$exe" "$work/source.bc" "$work/other.bc" -o "$work/linked.bc" \
            > "$work/output" 2>&1
        /usr/bin/llvm21-dis "$work/linked.bc" -o "$work/linked.ll"
        busy grep -Fq 'define i32 @m138_add' "$work/linked.ll"
        busy grep -Fq 'define i32 @m138_other' "$work/linked.ll" ;;
      extract)
        exe=$(original_elf /usr/bin/llvm21-extract)
        seed_bc
        "$exe" --func=m138_identity "$work/source.bc" -o "$work/extract.bc" \
            > "$work/output" 2>&1
        /usr/bin/llvm21-dis "$work/extract.bc" -o "$work/extract.ll"
        busy grep -Fq 'define i32 @m138_identity' "$work/extract.ll"
        ! busy grep -Fq 'define i32 @m138_add' "$work/extract.ll" ;;
      bcanalyzer)
        exe=$(original_elf /usr/bin/llvm21-bcanalyzer)
        seed_bc
        "$exe" -dump "$work/source.bc" > "$work/output" 2>&1
        busy grep -Fq 'MODULE_BLOCK' "$work/output"
        busy grep -Fq 'FUNCTION_BLOCK' "$work/output" ;;
      diff)
        exe=$(original_elf /usr/bin/llvm21-diff)
        seed_ir
        "$exe" "$work/source.ll" "$work/source.ll" > "$work/equal" 2>&1
        [ ! -s "$work/equal" ]
        busy sed 's/add i32 %x, 7/add i32 %x, 9/' \
            "$work/source.ll" > "$work/different.ll"
        "$exe" "$work/source.ll" "$work/different.ll" \
            > "$work/different" 2>&1 || :
        busy grep -Fq 'm138_add' "$work/different" ;;
      cat)
        exe=$(original_elf /usr/bin/llvm21-cat)
        seed_bc
        "$exe" "$work/source.bc" -o "$work/cat.bc" > "$work/output" 2>&1
        /usr/bin/llvm21-dis "$work/cat.bc" -o "$work/cat.ll"
        busy grep -Fq 'define i32 @m138_add' "$work/cat.ll" ;;
      cxxfilt)
        exe=$(original_elf /usr/bin/llvm21-cxxfilt)
        busy printf '_Z3fooi\n_ZN3Foo3barEv\n' | "$exe" > "$work/output"
        busy grep -Fxq 'foo(int)' "$work/output"
        busy grep -Fxq 'Foo::bar()' "$work/output" ;;
      dwarfdump)
        exe=$(original_elf /usr/bin/llvm21-dwarfdump)
        seed_asm
        /usr/bin/llvm21-mc -triple=x86_64-unknown-linux-gnu \
            -filetype=obj "$work/source.s" -o "$work/source.o"
        "$exe" --debug-line "$work/source.o" > "$work/output" 2>&1
        busy grep -Fq 'm138.c' "$work/output" ;;
      mc)
        exe=$(original_elf /usr/bin/llvm21-mc)
        seed_asm
        "$exe" -triple=x86_64-unknown-linux-gnu -filetype=obj \
            "$work/source.s" -o "$work/source.o" > "$work/output" 2>&1
        [ -s "$work/source.o" ]
        /usr/bin/llvm21-nm "$work/source.o" > "$work/symbols"
        busy grep -Fq m138_mcsymbol "$work/symbols" ;;
      mca)
        exe=$(original_elf /usr/bin/llvm21-mca)
        busy printf '%s\n' 'addq %rax, %rbx' 'xorq %rcx, %rcx' \
            > "$work/instructions.s"
        "$exe" -mtriple=x86_64-unknown-linux-gnu -mcpu=generic \
            -iterations=10 "$work/instructions.s" > "$work/output" 2>&1
        busy grep -Fq 'Iterations:' "$work/output"
        busy grep -Fq 'Total Cycles:' "$work/output" ;;
      objdump)
        exe=$(original_elf /usr/bin/llvm21-objdump)
        seed_obj
        "$exe" --disassemble "$work/source.o" > "$work/output" 2>&1
        busy grep -Fq 'm138_add' "$work/output"
        busy grep -Eq '(add|lea)' "$work/output" ;;
      nm)
        exe=$(original_elf /usr/bin/llvm21-nm)
        seed_obj
        "$exe" --defined-only "$work/source.o" > "$work/output" 2>&1
        busy grep -Fq m138_add "$work/output"
        busy grep -Fq m138_identity "$work/output" ;;
      size)
        exe=$(original_elf /usr/bin/llvm21-size)
        seed_obj
        "$exe" "$work/source.o" > "$work/output" 2>&1
        busy grep -Eq '^[[:space:]]*text[[:space:]]+data[[:space:]]+bss' "$work/output"
        busy grep -Fq 'source.o' "$work/output" ;;
      strings)
        exe=$(original_elf /usr/bin/llvm21-strings)
        seed_obj
        "$exe" "$work/source.o" > "$work/output" 2>&1
        busy grep -Fxq 'M138-LLVM' "$work/output" ;;
      objcopy)
        exe=$(original_elf /usr/bin/llvm21-objcopy)
        seed_obj
        busy printf 'M138_NEW_ELF_SECTION\n' > "$work/section.bin"
        "$exe" --add-section ".m138=$work/section.bin" \
            "$work/source.o" "$work/added.o" > "$work/output" 2>&1
        /usr/bin/llvm21-readobj --sections "$work/added.o" > "$work/sections"
        busy grep -Fq '.m138' "$work/sections" ;;
      profdata)
        exe=$(original_elf /usr/bin/llvm21-profdata)
        busy printf '%s\n' 'm138_add:100:0' ' 1: 100' \
            'm138_identity:20:0' ' 1: 20' > "$work/sample.txt"
        "$exe" merge -sample "$work/sample.txt" -o "$work/sample.profdata" \
            > "$work/output" 2>&1
        [ -s "$work/sample.profdata" ]
        "$exe" show -sample "$work/sample.profdata" > "$work/shown" 2>&1
        busy grep -Fq m138_add "$work/shown" ;;
      readobj)
        exe=$(original_elf /usr/bin/llvm21-readobj)
        seed_obj
        "$exe" --file-headers --sections "$work/source.o" \
            > "$work/output" 2>&1
        busy grep -Fq 'Format:' "$work/output"
        busy grep -Fq 'Arch:' "$work/output"
        busy grep -Fq '.text' "$work/output" ;;
      *) echo "[M138 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M138_TRACE:-0}" = 1 ]; then
        PS4='[M138 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in as dis opt llc link extract bcanalyzer diff cat cxxfilt \
           dwarfdump mc mca objdump nm size strings objcopy profdata readobj; do
    echo "[M138 CASE START] $app"
    case_log="/tmp/m138-$app-case.log"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    if M138_TRACE=1 /bin/sh /tests/m138-batch-run.sh --case "$app" \
          > "$case_log" 2>&1; then
        printf 'M138_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M138 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -170 "$case_log" >&2 || :
        for detail in output equal different shown sample.txt source.ll \
                      source.s source.o symbols sections result.ll linked.ll \
                      extract.ll cat.ll; do
            if [ -s "/tmp/m138-$app/$detail" ]; then
                echo "[M138 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 1300 "/tmp/m138-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M138_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M138 BATCH FAIL] $failed/20 original LLVM ELFs (full census)" >&2
    echo M138_BATCH_FAIL
    exit 1
fi
echo M138_BATCH_PASS
