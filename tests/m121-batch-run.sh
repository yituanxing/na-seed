#!/bin/sh
# Fifteen SIGNED ORIGINAL Linux PDF CLI executables, real semantic oracles.
# A two-page, Helvetica + Flate RGB input is constructed on the Linux HOST.
# NaSeed only loads and runs unmodified precompiled ELF, never builds software.
PATH=/usr/bin:/bin
HOME=/tmp
XDG_CONFIG_HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME XDG_CONFIG_HOME LC_ALL LANG
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
    echo "[M121 MISSING SIGNED ORIGINAL ELF] $*" >&2
    return 1
}

pdf=/tests/m121-original.pdf
invalid=/tests/m121-invalid.pdf
attachment=/tests/m121-attachment.txt

check_input() {
    [ -s "$pdf" ] && [ -s "$invalid" ] && [ -s "$attachment" ]
    magic=$(busy dd if="$pdf" bs=1 count=8 2>/dev/null)
    [ "$magic" = '%PDF-1.4' ]
}

assert_pdf_pages() {
    reader=$(original_elf /usr/bin/pdfinfo)
    "$reader" "$1" > "$work/pdfinfo"
    busy grep -Eq "^Pages:[[:space:]]*$2$" "$work/pdfinfo"
}

assert_text() {
    extractor=$(original_elf /usr/bin/pdftotext)
    "$extractor" -layout "$1" "$work/extracted.txt"
    busy grep -Fq "$2" "$work/extracted.txt"
}

assert_png_200() {
    file=$1
    [ -s "$file" ]
    sig=$(busy dd if="$file" bs=1 count=8 2>/dev/null |
          busy od -An -tx1 | busy tr -d ' \n')
    [ "$sig" = 89504e470d0a1a0a ]
    dimensions=$(busy dd if="$file" bs=1 skip=16 count=8 2>/dev/null |
                 busy od -An -tx1 | busy tr -d ' \n')
    [ "$dimensions" = 000000c8000000c8 ]
}

run_case() {
    app=$1
    work="/tmp/m121-$app"
    busy mkdir -p "$work"
    case "$app" in
      pdfattach)
        exe=$(original_elf /usr/bin/pdfattach)
        "$exe" "$pdf" "$attachment" "$work/attached.pdf"
        assert_pdf_pages "$work/attached.pdf" 2
        detached=$(original_elf /usr/bin/pdfdetach)
        "$detached" -save 1 -o "$work/recovered.txt" "$work/attached.pdf"
        busy cmp "$attachment" "$work/recovered.txt" ;;
      pdfdetach)
        exe=$(original_elf /usr/bin/pdfdetach)
        attach=$(original_elf /usr/bin/pdfattach)
        "$attach" "$pdf" "$attachment" "$work/attached.pdf"
        "$exe" -list "$work/attached.pdf" > "$work/list"
        busy grep -Fq 'm121-attachment.txt' "$work/list"
        "$exe" -save 1 -o "$work/recovered.txt" "$work/attached.pdf"
        busy cmp "$attachment" "$work/recovered.txt" ;;
      pdffonts)
        exe=$(original_elf /usr/bin/pdffonts)
        "$exe" "$pdf" > "$work/fonts"
        busy grep -Fq 'Helvetica' "$work/fonts"
        busy grep -Fq 'Type 1' "$work/fonts" ;;
      pdfimages)
        exe=$(original_elf /usr/bin/pdfimages)
        "$exe" -list "$pdf" > "$work/images"
        busy awk '$1 == 1 && $4 == 4 && $5 == 4 && $7 == 3 { found=1 } END { exit !found }' "$work/images" ;;
      pdfseparate)
        exe=$(original_elf /usr/bin/pdfseparate)
        "$exe" -f 1 -l 2 "$pdf" "$work/page-%d.pdf"
        [ -s "$work/page-1.pdf" ] && [ -s "$work/page-2.pdf" ]
        assert_pdf_pages "$work/page-1.pdf" 1
        assert_pdf_pages "$work/page-2.pdf" 1
        assert_text "$work/page-1.pdf" M121_PAGE_ONE_ALPHA
        assert_text "$work/page-2.pdf" M121_PAGE_TWO_BETA ;;
      pdfsig)
        exe=$(original_elf /usr/bin/pdfsig)
        # Poppler reports its expected "unsigned" condition with status 2.
        if "$exe" "$pdf" > "$work/signatures" 2>&1; then
            :
        else
            status=$?
            [ "$status" -eq 2 ] || return "$status"
        fi
        busy grep -Eiq '0 signature|no signature|not signed|does not contain.*signature' "$work/signatures" ;;
      pdftocairo)
        exe=$(original_elf /usr/bin/pdftocairo)
        "$exe" -png -f 1 -l 1 -r 72 -singlefile "$pdf" "$work/page"
        assert_png_200 "$work/page.png" ;;
      pdftohtml)
        exe=$(original_elf /usr/bin/pdftohtml)
        "$exe" -xml -i -hidden "$pdf" "$work/content" > "$work/messages" 2>&1
        [ -s "$work/content.xml" ]
        busy grep -Fq 'M121_PAGE_ONE_ALPHA' "$work/content.xml"
        busy grep -Fq 'M121_PAGE_TWO_BETA' "$work/content.xml" ;;
      pdftoppm)
        exe=$(original_elf /usr/bin/pdftoppm)
        "$exe" -f 1 -l 1 -scale-to 32 -singlefile "$pdf" "$work/preview"
        [ -s "$work/preview.ppm" ]
        busy grep -aq '^P6' "$work/preview.ppm"
        busy grep -aq '^32 32' "$work/preview.ppm" ;;
      pdftops)
        exe=$(original_elf /usr/bin/pdftops)
        "$exe" -f 1 -l 2 "$pdf" "$work/pages.ps"
        [ -s "$work/pages.ps" ]
        busy grep -Fq '%!PS-Adobe' "$work/pages.ps"
        busy grep -Eq '^%%Pages:[[:space:]]*2' "$work/pages.ps"
        busy grep -Fq 'showpage' "$work/pages.ps" ;;
      pdfunite)
        exe=$(original_elf /usr/bin/pdfunite)
        separate=$(original_elf /usr/bin/pdfseparate)
        "$separate" -f 1 -l 2 "$pdf" "$work/sheet-%d.pdf"
        "$exe" "$work/sheet-1.pdf" "$work/sheet-2.pdf" "$work/rejoined.pdf"
        assert_pdf_pages "$work/rejoined.pdf" 2
        assert_text "$work/rejoined.pdf" M121_PAGE_ONE_ALPHA
        busy grep -Fq 'M121_PAGE_TWO_BETA' "$work/extracted.txt" ;;
      mutool)
        exe=$(original_elf /usr/bin/mutool)
        "$exe" draw -F txt -o "$work/words.txt" "$pdf" 1-2
        busy grep -Fq 'M121_PAGE_ONE_ALPHA' "$work/words.txt"
        busy grep -Fq 'M121_PAGE_TWO_BETA' "$work/words.txt" ;;
      muraster)
        exe=$(original_elf /usr/bin/muraster)
        "$exe" -o "$work/render.ppm" -r 72,72 "$pdf" 1
        [ -s "$work/render.ppm" ]
        busy grep -aq '^P6' "$work/render.ppm"
        busy grep -aq '^200 200' "$work/render.ppm" ;;
      pdfgrep)
        exe=$(original_elf /usr/bin/pdfgrep)
        "$exe" -n M121_PAGE_ONE_ALPHA "$pdf" > "$work/hits"
        busy grep -Fq 'M121_PAGE_ONE_ALPHA' "$work/hits"
        if "$exe" -q M121_NO_SUCH_TEXT "$pdf"; then return 1; fi ;;
      pdfcpu)
        exe=$(original_elf /usr/bin/pdfcpu)
        if "$exe" --conf disable validate -m strict "$pdf" > "$work/valid" 2>&1; then
            :
        else
            status=$?
            echo '[M121 PDFCPU VALIDATION DIAGNOSTIC]' >&2
            busy cat "$work/valid" >&2
            return "$status"
        fi
        if "$exe" --conf disable validate "$invalid" > "$work/invalid" 2>&1; then
            return 1
        fi
        [ -s "$work/invalid" ] ;;
      *) echo "[M121 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M121_TRACE:-0}" = 1 ]; then
        PS4='[M121 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m121-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in pdfattach pdfdetach pdffonts pdfimages pdfseparate pdfsig \
           pdftocairo pdftohtml pdftoppm pdftops pdfunite mutool muraster \
           pdfgrep pdfcpu; do
    echo "[M121 CASE START] $app"
    case_log="/tmp/m121-$app-case.log"
    if M121_TRACE=1 /bin/sh /tests/m121-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M121_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M121 $upper FAILED] signed original Linux ELF, rc=$rc" >&2
        busy tail -115 "$case_log" >&2
        printf 'M121_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M121_BATCH_PASS
else
    echo "[M121 BATCH FAIL] $failed/15 original Linux PDF applications" >&2
    echo M121_BATCH_FAIL
    exit 1
fi
