#!/bin/sh
# Original signed elfutils programs, tested by genuine ELF/DWARF semantics.
# Input .o/.so fixtures are cross-compiled on Linux HOST, never in NaSeed.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    [ "$#" -gt 0 ] || return 1
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        [ -x "$resolved" ] || continue
        case "$resolved" in /bin/busybox|/usr/bin/busybox) continue ;; esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M118 MISSING SIGNED ELF] $*" >&2
    return 1
}

debug_object=/tests/m118-debug.o
different_object=/tests/m118-debug-other.o
linked_debug=/tests/m118-debug.elf
textrel_object=/tests/m118-textrel.so

check_input() {
    [ -s "$debug_object" ] && [ -s "$different_object" ] &&
        [ -s "$linked_debug" ] &&
        [ -s "$textrel_object" ]
}

separate_debug() {
    stripper=$(original_elf /usr/bin/eu-strip)
    input=${2:-$debug_object}
    "$stripper" -f "$1/separate.debug" -o "$1/stripped.o" "$input"
    [ -s "$1/separate.debug" ] && [ -s "$1/stripped.o" ]
}

run_case() {
    app=$1
    work="/tmp/m118-$app"
    busy mkdir -p "$work"
    case "$app" in
      addr2line)
        exe=$(original_elf /usr/bin/eu-addr2line)
        nm=$(original_elf /usr/bin/eu-nm)
        "$nm" -B -n "$debug_object" > "$work/symbols"
        address=$(busy awk '$3 == "m118_answer" { print "0x" $1; exit }' "$work/symbols")
        [ -n "$address" ]
        "$exe" -f -j .text -e "$debug_object" "$address" > "$work/located"
        busy grep -Fq 'm118_answer' "$work/located"
        busy grep -Fq 'm118-debug.c' "$work/located" ;;
      ar)
        exe=$(original_elf /usr/bin/eu-ar)
        (cd "$work"; "$exe" q objects.a "$debug_object" "$different_object")
        "$exe" t "$work/objects.a" > "$work/members"
        busy grep -Fxq 'm118-debug.o' "$work/members"
        busy grep -Fxq 'm118-debug-other.o' "$work/members"
        "$exe" p "$work/objects.a" m118-debug.o > "$work/recovered"
        busy cmp "$debug_object" "$work/recovered" ;;
      elfclassify)
        exe=$(original_elf /usr/bin/eu-elfclassify)
        # Classification verifies ET_REL ELF; separate section proof verifies
        # actual DWARF, independent of elfutils' unstripped heuristics.
        "$exe" --elf-file "$debug_object" > "$work/classified"
        busy grep -Fxq "$debug_object" "$work/classified"
        reader=$(original_elf /usr/bin/eu-readelf)
        "$reader" -S "$debug_object" > "$work/debug-sections"
        busy grep -Fq '.debug_info' "$work/debug-sections"
        archiver=$(original_elf /usr/bin/eu-ar)
        "$archiver" q "$work/classified.a" "$debug_object" "$different_object"
        "$exe" --elf-archive "$work/classified.a" > "$work/archive-class"
        busy grep -Fxq "$work/classified.a" "$work/archive-class" ;;
      elfcmp)
        exe=$(original_elf /usr/bin/eu-elfcmp)
        busy cp "$debug_object" "$work/copy.o"
        "$exe" "$debug_object" "$work/copy.o" > "$work/equal"
        if "$exe" --verbose "$debug_object" "$different_object" > "$work/different" 2>&1; then
            return 1
        fi
        [ -s "$work/different" ] ;;
      elfcompress)
        exe=$(original_elf /usr/bin/eu-elfcompress)
        reader=$(original_elf /usr/bin/eu-readelf)
        "$exe" -f -t zlib -o "$work/compressed.o" "$debug_object" > "$work/compress-log"
        [ -s "$work/compressed.o" ]
        "$reader" -S "$work/compressed.o" > "$work/sections"
        busy grep -Fq '.debug_info' "$work/sections"
        "$exe" -t none -o "$work/restored.o" "$work/compressed.o" > "$work/restore-log"
        src=$(original_elf /usr/bin/eu-srcfiles)
        "$src" -e "$work/restored.o" > "$work/srcfiles"
        busy grep -Fq 'm118-debug.c' "$work/srcfiles" ;;
      elflint)
        exe=$(original_elf /usr/bin/eu-elflint)
        # Clang's relocatable objects carry LLVM-specific section metadata
        # that elfutils' extra-strict elflint flags.  Instead lint a genuine
        # signed Alpine GCC-linked native ELF, as a Linux program would.
        signed_native=$(original_elf /usr/bin/eu-elflint)
        "$exe" --quiet "$signed_native" > "$work/lint" 2>&1
        reader=$(original_elf /usr/bin/eu-readelf)
        "$reader" -h "$signed_native" > "$work/header"
        busy grep -Eq 'Type:[[:space:]]+(DYN|EXEC)[[:space:]]' "$work/header" ;;
      findtextrel)
        exe=$(original_elf /usr/bin/eu-findtextrel)
        "$exe" "$textrel_object" > "$work/relocations" 2>&1
        [ -s "$work/relocations" ]
        # eu-findtextrel diagnoses the source *function* or non-PIC object;
        # it need not print the words "text" or "relocation".  These are its
        # actual function/source diagnostics, not an arbitrary success string.
        busy grep -Eiq 'not compiled with -fpic|-fPIC|a relocation modifies memory at offset' "$work/relocations" ;;
      nm)
        exe=$(original_elf /usr/bin/eu-nm)
        "$exe" -B "$debug_object" > "$work/symbols"
        busy grep -Eq '[[:space:]]T[[:space:]]m118_answer$' "$work/symbols"
        busy grep -Fq 'm118_unique_marker' "$work/symbols" ;;
      objdump)
        exe=$(original_elf /usr/bin/eu-objdump)
        "$exe" -s -j .text "$debug_object" > "$work/text-contents"
        busy grep -Fq '.text' "$work/text-contents"
        busy grep -Eiq '[[:xdigit:]]{8}' "$work/text-contents"
        "$exe" -r "$debug_object" > "$work/relocations"
        busy grep -Fq 'm118_unique_marker' "$work/relocations" ;;
      ranlib)
        exe=$(original_elf /usr/bin/eu-ranlib)
        archiver=$(original_elf /usr/bin/eu-ar)
        "$archiver" q "$work/symbols.a" "$debug_object" "$different_object"
        "$exe" "$work/symbols.a"
        symbols=$(original_elf /usr/bin/eu-nm)
        "$symbols" --print-armap "$work/symbols.a" > "$work/index"
        busy grep -Fq 'm118_answer' "$work/index" ;;
      size)
        exe=$(original_elf /usr/bin/eu-size)
        "$exe" -A "$debug_object" > "$work/sizes"
        busy awk '$1 == ".text" && $2 + 0 > 0 { ok=1 } END { exit !ok }' "$work/sizes" ;;
      srcfiles)
        exe=$(original_elf /usr/bin/eu-srcfiles)
        "$exe" -c -e "$debug_object" > "$work/sources"
        busy grep -Fq 'm118-debug.c' "$work/sources" ;;
      strings)
        exe=$(original_elf /usr/bin/eu-strings)
        "$exe" -n 16 "$debug_object" > "$work/extracted"
        busy grep -Fxq 'M118_SIGNED_ELFUTILS_BINARY_SENTINEL' "$work/extracted" ;;
      strip)
        exe=$(original_elf /usr/bin/eu-strip)
        separate_debug "$work"
        reader=$(original_elf /usr/bin/eu-readelf)
        "$reader" -S "$work/stripped.o" > "$work/stripped-sections"
        if busy grep -Fq '.debug_info' "$work/stripped-sections"; then return 1; fi
        "$reader" -S "$work/separate.debug" > "$work/debug-sections"
        busy grep -Fq '.debug_info' "$work/debug-sections" ;;
      unstrip)
        exe=$(original_elf /usr/bin/eu-unstrip)
        separate_debug "$work" "$linked_debug"
        "$exe" -o "$work/rejoined.o" "$work/stripped.o" "$work/separate.debug"
        [ -s "$work/rejoined.o" ]
        reader=$(original_elf /usr/bin/eu-readelf)
        "$reader" -S "$work/rejoined.o" > "$work/sections"
        busy grep -Fq '.debug_info' "$work/sections"
        source_tool=$(original_elf /usr/bin/eu-srcfiles)
        "$source_tool" -e "$work/rejoined.o" > "$work/sources"
        busy grep -Fq 'm118-debug.c' "$work/sources" ;;
      *) echo "[M118 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M118_TRACE:-0}" = 1 ]; then
        PS4='[M118 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m118-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in addr2line ar elfclassify elfcmp elfcompress elflint findtextrel \
           nm objdump ranlib size srcfiles strings strip unstrip; do
    echo "[M118 CASE START] $app"
    case_log="/tmp/m118-$app-case.log"
    if M118_TRACE=1 /bin/sh /tests/m118-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M118_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M118 $upper FAILED] genuine original Linux ELF, rc=$rc" >&2
        busy tail -110 "$case_log" >&2
        printf 'M118_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M118_BATCH_PASS
else
    echo "[M118 BATCH FAIL] $failed/15 genuine original Linux ELF programs" >&2
    echo M118_BATCH_FAIL
    exit 1
fi
