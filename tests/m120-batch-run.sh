#!/bin/sh
# Fifteen ORIGINAL signed Alpine LibTIFF programs performing real image work.
# All image fixtures were created on the Linux host, not inside NaSeed.
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
    echo "[M120 MISSING SIGNED NATIVE ELF] $*" >&2
    return 1
}

check_input() {
    [ -s /tests/m120-color.ppm ] &&
    [ -s /tests/m120-inverted.ppm ] &&
    [ -s /tests/m120-gray.pgm ] &&
    [ -s /tests/m120-gray.raw ] &&
    [ "$(busy wc -c < /tests/m120-gray.raw)" -eq 256 ]
}

make_source_tiffs() {
    ppm=$(original_elf /usr/bin/ppm2tiff)
    "$ppm" -c none /tests/m120-color.ppm "$work/color.tif"
    "$ppm" -c none /tests/m120-gray.pgm "$work/gray.tif"
    "$ppm" -c none /tests/m120-inverted.ppm "$work/inverted.tif"
    [ -s "$work/color.tif" ] && [ -s "$work/gray.tif" ] &&
        [ -s "$work/inverted.tif" ]
}

assert_size() {
    # Independent original TIFF directory parser confirms geometry.
    reader=$(original_elf /usr/bin/tiffinfo)
    "$reader" -D "$1" > "$work/directory"
    busy grep -Eq "Image Width: *$2 Image Length: *$3" "$work/directory"
}

run_case() {
    app=$1
    work="/tmp/m120-$app"
    busy mkdir -p "$work"
    make_source_tiffs
    case "$app" in
      ppm2tiff)
        exe=$(original_elf /usr/bin/ppm2tiff)
        "$exe" -c zip /tests/m120-color.ppm "$work/zipped.tif"
        assert_size "$work/zipped.tif" 16 16
        busy grep -Fq 'Samples/Pixel: 3' "$work/directory"
        busy grep -Fq 'Compression Scheme: AdobeDeflate' "$work/directory" ||
            busy grep -Fq 'Compression Scheme: Deflate' "$work/directory"
        cmp=$(original_elf /usr/bin/tiffcmp)
        "$cmp" -t "$work/color.tif" "$work/zipped.tif" ;;
      raw2tiff)
        exe=$(original_elf /usr/bin/raw2tiff)
        "$exe" -w 16 -b 1 -d byte -p minisblack -c none \
            /tests/m120-gray.raw "$work/from-raw.tif"
        assert_size "$work/from-raw.tif" 16 16
        cmp=$(original_elf /usr/bin/tiffcmp)
        "$cmp" -t "$work/gray.tif" "$work/from-raw.tif" ;;
      tiff2bw)
        exe=$(original_elf /usr/bin/tiff2bw)
        "$exe" -c none "$work/color.tif" "$work/bw.tif"
        assert_size "$work/bw.tif" 16 16
        busy grep -Fq 'Samples/Pixel: 1' "$work/directory"
        busy grep -Fq 'Photometric Interpretation: min-is-black' "$work/directory" ;;
      tiff2pdf)
        exe=$(original_elf /usr/bin/tiff2pdf)
        "$exe" -t M120-NaSeed-Original-TIFF -o "$work/document.pdf" "$work/color.tif"
        [ -s "$work/document.pdf" ]
        busy grep -aq '%PDF-' "$work/document.pdf"
        busy grep -aq '/Type /Page' "$work/document.pdf"
        busy grep -aq 'M120-NaSeed-Original-TIFF' "$work/document.pdf"
        busy grep -aq 'xref' "$work/document.pdf" ;;
      tiff2ps)
        exe=$(original_elf /usr/bin/tiff2ps)
        "$exe" -p -t M120-Original-TIFF "$work/color.tif" > "$work/document.ps"
        [ -s "$work/document.ps" ]
        busy grep -Fq '%!PS-Adobe' "$work/document.ps"
        busy grep -Fq 'M120-Original-TIFF' "$work/document.ps"
        busy grep -Fq 'colorimage' "$work/document.ps"
        busy grep -Fq 'showpage' "$work/document.ps" ;;
      tiff2rgba)
        exe=$(original_elf /usr/bin/tiff2rgba)
        "$exe" -c none "$work/color.tif" "$work/rgba.tif"
        assert_size "$work/rgba.tif" 16 16
        busy grep -Fq 'Samples/Pixel: 4' "$work/directory"
        busy grep -Fq 'Bits/Sample: 8' "$work/directory" ;;
      tiffcmp)
        exe=$(original_elf /usr/bin/tiffcmp)
        cp_tool=$(original_elf /usr/bin/tiffcp)
        "$cp_tool" -c lzw "$work/color.tif" "$work/recoded.tif"
        "$exe" -t "$work/color.tif" "$work/recoded.tif" > "$work/equal"
        if "$exe" -t "$work/color.tif" "$work/inverted.tif" > "$work/different" 2>&1; then
            echo '[M120 TIFFCMP INVALID] different pixels mistakenly equal' >&2
            return 1
        fi
        [ -s "$work/different" ] ;;
      tiffcp)
        exe=$(original_elf /usr/bin/tiffcp)
        "$exe" -c lzw "$work/color.tif" "$work/recoded.tif"
        assert_size "$work/recoded.tif" 16 16
        busy grep -Fq 'Compression Scheme: LZW' "$work/directory"
        cmp=$(original_elf /usr/bin/tiffcmp)
        "$cmp" -t "$work/color.tif" "$work/recoded.tif" ;;
      tiffcrop)
        exe=$(original_elf /usr/bin/tiffcrop)
        "$exe" -U px -E left -X 8 -Y 16 "$work/color.tif" "$work/half.tif"
        assert_size "$work/half.tif" 8 16
        busy grep -Fq 'Samples/Pixel: 3' "$work/directory" ;;
      tiffdither)
        exe=$(original_elf /usr/bin/tiffdither)
        "$exe" -c none "$work/gray.tif" "$work/bilevel.tif"
        assert_size "$work/bilevel.tif" 16 16
        busy grep -Fq 'Bits/Sample: 1' "$work/directory"
        busy grep -Fq 'Samples/Pixel: 1' "$work/directory" ;;
      tiffdump)
        exe=$(original_elf /usr/bin/tiffdump)
        "$exe" "$work/color.tif" > "$work/tags"
        busy grep -Fq 'ImageWidth (256)' "$work/tags"
        busy grep -Fq 'ImageLength (257)' "$work/tags"
        busy grep -Fq '1<16>' "$work/tags" ;;
      tiffinfo)
        exe=$(original_elf /usr/bin/tiffinfo)
        "$exe" -D "$work/color.tif" > "$work/full"
        busy grep -Eq 'Image Width: *16 Image Length: *16' "$work/full"
        busy grep -Fq 'Samples/Pixel: 3' "$work/full"
        busy grep -Fq 'Photometric Interpretation: RGB color' "$work/full" ;;
      tiffmedian)
        exe=$(original_elf /usr/bin/tiffmedian)
        "$exe" -C 16 -c none "$work/color.tif" "$work/palette.tif"
        assert_size "$work/palette.tif" 16 16
        busy grep -Eiq 'Photometric Interpretation: .*palette' "$work/directory"
        busy grep -Fq 'Samples/Pixel: 1' "$work/directory" ;;
      tiffset)
        exe=$(original_elf /usr/bin/tiffset)
        busy cp "$work/color.tif" "$work/annotated.tif"
        "$exe" -s 315 NaSeed-M120-Original-TIFF "$work/annotated.tif"
        reader=$(original_elf /usr/bin/tiffinfo)
        "$reader" "$work/annotated.tif" > "$work/updated"
        busy grep -Fq 'Artist: NaSeed-M120-Original-TIFF' "$work/updated"
        "$exe" -u 315 "$work/annotated.tif"
        "$reader" "$work/annotated.tif" > "$work/removed"
        if busy grep -Fq 'Artist: NaSeed-M120-Original-TIFF' "$work/removed"; then
            return 1
        fi ;;
      tiffsplit)
        exe=$(original_elf /usr/bin/tiffsplit)
        cp_tool=$(original_elf /usr/bin/tiffcp)
        "$cp_tool" "$work/color.tif" "$work/gray.tif" "$work/album.tif"
        "$exe" "$work/album.tif" "$work/frame"
        # Recent upstream tiffsplit emits frame0000001.tif/frame0000002.tif;
        # older releases use frameaaa.tif/frameaab.tif.  Verify the actual
        # exactly two split pages and compare decoded pixel contents instead
        # of assuming a particular filename suffix convention.
        set -- "$work"/frame*.tif
        [ "$#" -eq 2 ] && [ -s "$1" ] && [ -s "$2" ]
        cmp=$(original_elf /usr/bin/tiffcmp)
        "$cmp" -t "$work/color.tif" "$1"
        "$cmp" -t "$work/gray.tif" "$2" ;;
      *) echo "[M120 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M120_TRACE:-0}" = 1 ]; then
        PS4='[M120 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m120-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in ppm2tiff raw2tiff tiff2bw tiff2pdf tiff2ps tiff2rgba tiffcmp \
           tiffcp tiffcrop tiffdither tiffdump tiffinfo tiffmedian tiffset tiffsplit; do
    echo "[M120 CASE START] $app"
    case_log="/tmp/m120-$app-case.log"
    if M120_TRACE=1 /bin/sh /tests/m120-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M120_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M120 $upper FAILED] original LibTIFF Linux ELF, rc=$rc" >&2
        busy tail -110 "$case_log" >&2
        printf 'M120_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M120_BATCH_PASS
else
    echo "[M120 BATCH FAIL] $failed/15 independent original Linux ELF programs" >&2
    echo M120_BATCH_FAIL
    exit 1
fi
