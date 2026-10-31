#!/bin/sh
# Fifteen distinct signed Alpine GNU groff ELF programs with semantic oracles.
# All source files arrive from Linux host. NaSeed does no native compilation.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
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
    echo "[M126 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

check_inputs() {
    for name in basic.roff blank.roff equation.roff picture.roff \
                table.roff include.roff soelim.roff utf8.roff \
                references.bib refer.roff lookbib.query; do
        [ -s "/tests/m126-$name" ] || return 1
    done
}

has() { busy grep -Fq "$2" "$1"; }
not_has() { ! busy grep -Fq "$2" "$1"; }
nonempty() { [ -s "$1" ]; }

assert_troff_intermediate() {
    has "$1" 'x T '
    has "$1" 'M126QUARTZTEXT'
}

assert_job_difference() {
    nonempty "$1"
    ! busy cmp -s "$1" "$2"
}

run_case() {
    app=$1
    work="/tmp/m126-$app"
    busy mkdir -p "$work"
    basic=/tests/m126-basic.roff
    blank=/tests/m126-blank.roff
    bib=/tests/m126-references.bib
    case "$app" in
      groff)
        exe=$(original_elf /usr/bin/groff)
        "$exe" -Tascii "$basic" > "$work/output.txt"
        has "$work/output.txt" M126DOCUMENTOPEN
        has "$work/output.txt" M126QUARTZTEXT
        has "$work/output.txt" M126DOCUMENTEND ;;
      troff)
        exe=$(original_elf /usr/bin/troff)
        "$exe" -Tascii "$basic" > "$work/output.tr"
        assert_troff_intermediate "$work/output.tr"
        renderer=$(original_elf /usr/bin/grotty)
        "$renderer" "$work/output.tr" > "$work/visible.txt"
        has "$work/visible.txt" M126QUARTZTEXT ;;
      grotty)
        exe=$(original_elf /usr/bin/grotty)
        formatter=$(original_elf /usr/bin/troff)
        "$formatter" -Tascii "$basic" > "$work/input.tr"
        "$exe" "$work/input.tr" > "$work/visible.txt"
        has "$work/visible.txt" M126DOCUMENTOPEN
        has "$work/visible.txt" M126DOCUMENTEND ;;
      grops)
        exe=$(original_elf /usr/bin/grops)
        formatter=$(original_elf /usr/bin/troff)
        "$formatter" -Tps "$basic" > "$work/input.tr"
        "$exe" "$work/input.tr" > "$work/output.ps"
        nonempty "$work/output.ps"
        has "$work/output.ps" '%!PS-Adobe'
        # PostScript driver applies kerning and may split text into fragments.
        has "$work/output.ps" 'M126DOCUMENTEND'
        has "$work/output.ps" '%%Page:' ;;
      grolbp)
        exe=$(original_elf /usr/bin/grolbp)
        formatter=$(original_elf /usr/bin/troff)
        "$formatter" -Tlbp "$basic" > "$work/input.tr"
        "$formatter" -Tlbp "$blank" > "$work/blank.tr"
        "$exe" "$work/input.tr" > "$work/output.capsl"
        "$exe" "$work/blank.tr" > "$work/blank.capsl"
        # Binary Canon CaPSL page content must differ from empty document.
        assert_job_difference "$work/output.capsl" "$work/blank.capsl"
        [ "$(busy wc -c < "$work/output.capsl")" -gt 80 ] ;;
      grodvi)
        exe=$(original_elf /usr/bin/grodvi)
        formatter=$(original_elf /usr/bin/troff)
        "$formatter" -Tdvi "$basic" > "$work/input.tr"
        "$formatter" -Tdvi "$blank" > "$work/blank.tr"
        "$exe" "$work/input.tr" > "$work/output.dvi"
        "$exe" "$work/blank.tr" > "$work/blank.dvi"
        assert_job_difference "$work/output.dvi" "$work/blank.dvi"
        magic=$(busy dd if="$work/output.dvi" bs=1 count=2 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = f702 ] ;;
      eqn)
        exe=$(original_elf /usr/bin/eqn)
        "$exe" -Tascii /tests/m126-equation.roff > "$work/transformed.roff"
        not_has "$work/transformed.roff" 'x over y'
        has "$work/transformed.roff" M126EQUATIONSTART
        ! busy cmp -s /tests/m126-equation.roff "$work/transformed.roff"
        renderer=$(original_elf /usr/bin/groff)
        "$renderer" -Tascii "$work/transformed.roff" > "$work/formatted.txt"
        has "$work/formatted.txt" M126EQUATIONSTART
        has "$work/formatted.txt" M126EQUATIONEND ;;
      pic)
        exe=$(original_elf /usr/bin/pic)
        "$exe" -Tps /tests/m126-picture.roff > "$work/transformed.roff"
        has "$work/transformed.roff" M126DIAGRAM
        has "$work/transformed.roff" '\D'
        not_has "$work/transformed.roff" 'box "M126DIAGRAM"'
        ! busy cmp -s /tests/m126-picture.roff "$work/transformed.roff" ;;
      tbl)
        exe=$(original_elf /usr/bin/tbl)
        "$exe" /tests/m126-table.roff > "$work/transformed.roff"
        has "$work/transformed.roff" M126PLANET
        has "$work/transformed.roff" M126MARS
        not_has "$work/transformed.roff" 'tab(|);'
        renderer=$(original_elf /usr/bin/groff)
        "$renderer" -Tascii "$work/transformed.roff" > "$work/formatted.txt"
        has "$work/formatted.txt" M126MARS
        has "$work/formatted.txt" M126PLANET ;;
      refer)
        exe=$(original_elf /usr/bin/refer)
        "$exe" -p "$bib" /tests/m126-refer.roff > "$work/resolved.roff"
        has "$work/resolved.roff" M126CITATIONBEGIN
        has "$work/resolved.roff" M126CITATIONEND
        has "$work/resolved.roff" 'M126SNOWFLAKE Quantum System Design'
        has "$work/resolved.roff" 'Ada Kernel'
        not_has "$work/resolved.roff" 'M126LANTERN Thread Scheduling' ;;
      soelim)
        exe=$(original_elf /usr/bin/soelim)
        "$exe" /tests/m126-soelim.roff > "$work/expanded.roff"
        has "$work/expanded.roff" M126BEFOREINCLUDE
        has "$work/expanded.roff" M126INCLUDEDLINE
        has "$work/expanded.roff" M126AFTERINCLUDE
        not_has "$work/expanded.roff" '.so /tests/m126-include.roff' ;;
      preconv)
        exe=$(original_elf /usr/bin/preconv)
        "$exe" -e UTF-8 /tests/m126-utf8.roff > "$work/converted.roff"
        has "$work/converted.roff" M126UNICODE
        # The UTF-8 two-byte character is translated to groff's ASCII escape.
        busy grep -Eq '\\\[[^]]+\]' "$work/converted.roff"
        ! busy cmp -s /tests/m126-utf8.roff "$work/converted.roff" ;;
      indxbib)
        exe=$(original_elf /usr/bin/indxbib)
        busy cp "$bib" "$work/references.bib"
        "$exe" "$work/references.bib"
        nonempty "$work/references.bib.i"
        query=$(original_elf /usr/bin/lkbib)
        "$query" -n -p "$work/references.bib" M126SNOWFLAKE > "$work/matched.txt"
        has "$work/matched.txt" 'M126SNOWFLAKE Quantum System Design'
        not_has "$work/matched.txt" M126LANTERN ;;
      lkbib)
        exe=$(original_elf /usr/bin/lkbib)
        "$exe" -n -p "$bib" M126SNOWFLAKE > "$work/matched.txt"
        has "$work/matched.txt" 'Ada Kernel'
        has "$work/matched.txt" 'M126SNOWFLAKE Quantum System Design'
        not_has "$work/matched.txt" M126LANTERN ;;
      lookbib)
        exe=$(original_elf /usr/bin/lookbib)
        "$exe" "$bib" < /tests/m126-lookbib.query > "$work/matched.txt"
        has "$work/matched.txt" 'Ada Kernel'
        has "$work/matched.txt" 'M126SNOWFLAKE Quantum System Design'
        not_has "$work/matched.txt" M126LANTERN ;;
      *) echo "[M126 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -gt 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_inputs
    if [ "${M126_TRACE:-0}" = 1 ]; then
        PS4='[M126 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m126-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in groff troff grotty grops grolbp grodvi eqn pic tbl refer \
           soelim preconv indxbib lkbib lookbib; do
    echo "[M126 CASE START] $app"
    case_log="/tmp/m126-$app-case.log"
    if M126_TRACE=1 /bin/sh /tests/m126-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M126_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M126 $upper FAILED] original ELF rc=$rc" >&2
        busy tail -120 "$case_log" >&2
        printf 'M126_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M126_BATCH_PASS
else
    echo "[M126 BATCH FAIL] $failed/15 GNU groff programs" >&2
    echo M126_BATCH_FAIL
    exit 1
fi
