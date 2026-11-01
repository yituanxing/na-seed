#!/bin/sh
# Original unmodified signed Alpine ELF binaries; every case consumes genuine
# fixture content and checks a semantic result, never merely --help/version.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
object=/tests/m153-native.o
changed=/tests/m153-native-v2.o
source=/tests/m153-fixture.c
style=/tests/m153-style.c
rule=/tests/m153-rule.yar
original_elf() {
    executable=$1
    [ -x "$executable" ] || return 1
    resolved=$(busy readlink -f "$executable") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null | \
        busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$executable"
}
nonempty() { [ -s "$work/$1" ]; }
contains() { busy grep -Fq "$2" "$work/$1"; }
case_run() {
    app=$1
    work="/tmp/m153-$app"
    busy mkdir -p "$work"
    case "$app" in
      clang-format|clang-check|clang-tidy)
        exe=$(original_elf "/usr/lib/llvm21/bin/$app") ;;
      *) exe=$(original_elf "/usr/bin/$app") ;;
    esac
    case "$app" in
      radare2)
        # Raw IO mode + an explicit seek: default ELF analysis starts at its
        # first mapped instruction, not the raw file's ELF-header offset zero.
        "$exe" -n -q0 -e scr.color=0 -c 's 0; p8 4' "$object" > "$work/result" 2> "$work/error" || return 1
        busy grep -iq '7f454c46' "$work/result" ;;
      rabin2)
        "$exe" -s "$object" > "$work/result" 2> "$work/error" || return 1
        contains result m153_add ;;
      radiff2)
        "$exe" "$object" "$changed" > "$work/result" 2> "$work/error" || :
        nonempty result && contains result '=>' ;;
      rafind2)
        "$exe" -s M153_C_MARKER "$object" > "$work/result" 2> "$work/error" || return 1
        nonempty result ;;
      rahash2)
        "$exe" -a sha256 "$object" > "$work/result" 2> "$work/error" || return 1
        expected=$(busy sha256sum "$object" | busy awk '{print $1}')
        contains result "$expected" ;;
      rasm2)
        "$exe" -a x86 -b 64 'nop' > "$work/result" 2> "$work/error" || return 1
        busy grep -Eq '^90$' "$work/result" ;;
      rax2)
        "$exe" 0x10 > "$work/result" 2> "$work/error" || return 1
        busy grep -Eq '^16$' "$work/result" ;;
      pahole)
        # pahole v1.30 can print the complete DWARF structure and then
        # report a secondary BTF-format error if the object has no .BTF.
        # Check the actual layout fields, never accept an empty/error result.
        "$exe" -F dwarf -C M153_Record "$object" > "$work/result" 2> "$work/error" || :
        contains result 'struct M153_Record' && contains result 'count;' &&
        contains result 'code;' && contains result 'name[20]' ;;
      pfunct)
        "$exe" "$object" > "$work/result" 2> "$work/error" || return 1
        contains result m153_add ;;
      pglobal)
        "$exe" --variables "$object" > "$work/result" 2> "$work/error" || return 1
        contains result m153_record ;;
      pdwtags)
        "$exe" "$object" > "$work/result" 2> "$work/error" || return 1
        nonempty result && contains result M153_Record ;;
      dtagnames)
        "$exe" "$object" > "$work/result" 2> "$work/error" || return 1
        nonempty result && busy grep -Eiq '(structure|variable|subprogram|DW_TAG)' "$work/result" ;;
      codiff)
        "$exe" "$object" "$changed" > "$work/result" 2> "$work/error" || :
        nonempty result && contains result M153_Record ;;
      errno)
        "$exe" 13 > "$work/result" 2> "$work/error" || return 1
        contains result EACCES ;;
      ifne)
        "$exe" /bin/busybox cat < /tests/m153-text.txt > "$work/result" 2> "$work/error" || return 1
        busy cmp -s /tests/m153-text.txt "$work/result" ;;
      isutf8)
        "$exe" /tests/m153-text.txt > "$work/result" 2> "$work/error" || return 1
        busy printf '\377' > "$work/invalid"
        if "$exe" "$work/invalid" > "$work/invalid-result" 2>&1; then return 1; fi ;;
      lckdo)
        "$exe" "$work/lock" /bin/busybox echo M153_LOCKED > "$work/result" 2> "$work/error" || return 1
        contains result M153_LOCKED ;;
      mispipe)
        if "$exe" '/bin/busybox false' '/bin/busybox true' > "$work/result" 2> "$work/error"; then
            return 1
        fi ;;
      parallel-moreutils)
        "$exe" -j 2 /bin/busybox echo -- M153_ALPHA M153_BETA > "$work/result" 2> "$work/error" || return 1
        contains result M153_ALPHA && contains result M153_BETA ;;
      pee)
        "$exe" "/bin/busybox cat > $work/left" "/bin/busybox cat > $work/right" \
            < /tests/m153-text.txt > "$work/result" 2> "$work/error" || return 1
        busy cmp -s /tests/m153-text.txt "$work/left" &&
        busy cmp -s /tests/m153-text.txt "$work/right" ;;
      nasm)
        "$exe" -f bin -o "$work/code.bin" /tests/m153-assembly.asm > "$work/result" 2> "$work/error" || return 1
        actual=$(busy od -An -tx1 "$work/code.bin" | busy tr -d ' \n')
        [ "$actual" = 90c3 ] ;;
      ndisasm)
        # The original decoder consumes actual x86 instructions, not --help.
        busy printf '\220\303' > "$work/code.bin"
        "$exe" -b 64 "$work/code.bin" > "$work/result" 2> "$work/error" || return 1
        busy grep -Eiq '[[:space:]]NOP$' "$work/result" &&
        busy grep -Eiq '[[:space:]]RET$' "$work/result" ;;
      yara)
        "$exe" "$rule" "$object" > "$work/result" 2> "$work/error" || return 1
        contains result M153_Compiled ;;
      yarac)
        "$exe" "$rule" "$work/compiled.yarc" > "$work/result" 2> "$work/error" || return 1
        nonempty compiled.yarc || return 1
        /usr/bin/yara -C "$work/compiled.yarc" "$object" > "$work/scan" 2>> "$work/error" || return 1
        contains scan M153_Compiled ;;
      bloaty)
        "$exe" -d sections "$object" > "$work/result" 2> "$work/error" || return 1
        contains result .text ;;
      cflow)
        "$exe" "$source" > "$work/result" 2> "$work/error" || return 1
        contains result m153_add && contains result main ;;
      difft)
        "$exe" --color=never /tests/m153-text.txt /tests/m153-text-v2.txt \
            > "$work/result" 2> "$work/error" || :
        contains result M153_C_MARKER && contains result M153_D_MARKER ;;
      mold)
        "$exe" -shared -o "$work/linked.so" "$object" > "$work/result" 2> "$work/error" || return 1
        nonempty linked.so || return 1
        magic=$(busy dd if="$work/linked.so" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 7f454c46 ] && /usr/bin/rabin2 -s "$work/linked.so" | busy grep -Fq m153_add ;;
      ccache)
        # Two identical real compilations must produce byte-identical objects
        # and leave at least one cache hit, exercising the actual cache engine.
        export CCACHE_DIR="$work/cache"
        busy mkdir -p "$CCACHE_DIR"
        "$exe" /usr/bin/gcc -g0 -O0 -c "$style" -o "$work/first.o" \
            > "$work/result" 2> "$work/error" || return 1
        "$exe" /usr/bin/gcc -g0 -O0 -c "$style" -o "$work/first.o" \
            >> "$work/result" 2>> "$work/error" || return 1
        nonempty first.o || return 1
        "$exe" --show-stats > "$work/stats" 2>> "$work/error" || return 1
        busy grep -Eiq '^[[:space:]]*(Cache hits|Hits):[[:space:]]*[1-9]' "$work/stats" ;;
      cpp)
        busy printf '%s\n' '#define M153_VALUE 37' 'M153_VALUE' > "$work/macro.c"
        "$exe" -P "$work/macro.c" > "$work/result" 2> "$work/error" || return 1
        busy grep -Fqx 37 "$work/result" ;;
      gcc-ar)
        "$exe" rcs "$work/libm153.a" "$object" > "$work/result" 2> "$work/error" || return 1
        /usr/bin/ar t "$work/libm153.a" > "$work/members" 2>> "$work/error" || return 1
        contains members m153-native.o ;;
      gcc-nm)
        "$exe" "$object" > "$work/result" 2> "$work/error" || return 1
        contains result m153_add && contains result m153_record ;;
      gcc-ranlib)
        /usr/bin/ar rc "$work/libm153.a" "$object" || return 1
        "$exe" "$work/libm153.a" > "$work/result" 2> "$work/error" || return 1
        /usr/bin/nm -s "$work/libm153.a" > "$work/symbols" 2>> "$work/error" || return 1
        contains symbols 'Archive index:' && contains symbols m153_add ;;
      clang-format)
        "$exe" "$style" > "$work/result" 2> "$work/error" || return 1
        contains result 'int m153_fmt() {' && contains result 'return 7;' ;;
      clang-check)
        "$exe" -ast-dump "$style" -- -x c > "$work/result" 2> "$work/error" || return 1
        contains result FunctionDecl && contains result m153_fmt ;;
      clang-tidy)
        "$exe" --checks=-\*,readability-magic-numbers "$style" -- -x c \
            > "$work/result" 2> "$work/error" || return 1
        (contains result 'magic number' || contains error 'magic number') &&
        (contains result 42 || contains error 42) ;;
      *) echo "[M153 UNKNOWN ELF] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in radare2 rabin2 radiff2 rafind2 rahash2 rasm2 rax2 \
           pahole pfunct pglobal pdwtags dtagnames codiff \
           errno ifne isutf8 lckdo mispipe parallel-moreutils pee \
           nasm ndisasm yara yarac bloaty cflow difft mold ccache \
           cpp gcc-ar gcc-nm gcc-ranlib clang-format clang-check clang-tidy; do
    echo "[M153 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m153-$app-case.log"
    if /bin/sh /tests/m153-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M153_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M153 ORIGINAL ELF FAILED] $app rc=$rc" >&2
        busy tail -80 "$detail" >&2 || :
        for artifact in error result changed scan symbols members; do
            if [ -s "/tmp/m153-$app/$artifact" ]; then
                echo "[M153 FAILURE DETAIL] $app/$artifact" >&2
                busy head -c 500 "/tmp/m153-$app/$artifact" >&2 || :
                echo >&2
            fi
        done
        printf 'M153_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M153 BATCH FAIL] $failures/36 authentic Alpine ELF behavior cases" >&2
    echo M153_BATCH_FAIL
    exit 1
fi
echo M153_BATCH_PASS
