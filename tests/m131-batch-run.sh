#!/bin/sh
# 15 new signed, mutually distinct Netpbm ELFs. Each case checks a REAL
# conversion or pixel/dimension transformation, not --help or version text.
# Failures are collected across all 15 before the batch returns nonzero.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
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

seed_image() {
    dir=$1
    # Four independent RGB samples, not a zero-filled artificial image.
    busy printf 'P3\n2 2\n255\n255 0 0\n0 255 0\n0 0 255\n255 255 0\n' > "$dir/rgb.ppm"
    busy printf 'P3\n3 2\n255\n255 0 0\n0 255 0\n0 0 255\n255 255 0\n0 255 255\n255 0 255\n' > "$dir/rect.ppm"
    busy printf 'P1\n2 2\n0 1\n1 0\n' > "$dir/mono.pbm"
    busy printf 'P2\n2 2\n255\n30 90\n160 220\n' > "$dir/gray.pgm"
    busy printf 'P3\n2 2\n255\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n' > "$dir/black.ppm"
    busy printf 'P3\n4 4\n255\n' > "$dir/border.ppm"
    y=0
    while [ "$y" -lt 4 ]; do
        x=0
        while [ "$x" -lt 4 ]; do
            if [ "$x" -eq 0 ] || [ "$x" -eq 3 ] ||
               [ "$y" -eq 0 ] || [ "$y" -eq 3 ]; then
                busy printf '255 255 255\n' >> "$dir/border.ppm"
            else
                busy printf '0 0 0\n' >> "$dir/border.ppm"
            fi
            x=$((x+1))
        done
        y=$((y+1))
    done
}
meta() {
    probe=$(original_elf /usr/bin/pamfile)
    "$probe" -machine "$1" > "$work/metadata"
}
assert_image() {
    meta "$1"
    busy awk -v fmt="$2" -v w="$3" -v h="$4" \
        '$2==fmt && $4==w && $5==h {okay=1} END {exit !okay}' \
        "$work/metadata"
}
same_pixels() {
    normalizer=$(original_elf /usr/bin/pamtopnm)
    "$normalizer" -plain "$1" > "$work/a.plain"
    "$normalizer" -plain "$2" > "$work/b.plain"
    busy cmp -s "$work/a.plain" "$work/b.plain"
}

run_case() {
    app=$1
    work="/tmp/m131-$app"
    busy mkdir -p "$work"
    seed_image "$work"
    case "$app" in
      pamtopam)
        exe=$(original_elf /usr/bin/pamtopam)
        # pamtopam's upstream syntax is STDIN only; a filename is invalid.
        "$exe" < "$work/rgb.ppm" > "$work/output.pam" 2> "$work/error"
        [ "$(busy head -c 2 "$work/output.pam")" = P7 ]
        assert_image "$work/output.pam" PAM 2 2
        same_pixels "$work/output.pam" "$work/rgb.ppm" ;;
      pamtopnm)
        exe=$(original_elf /usr/bin/pamtopnm)
        /usr/bin/pamtopam < "$work/rgb.ppm" > "$work/image.pam"
        "$exe" "$work/image.pam" > "$work/output.ppm" 2> "$work/error"
        assert_image "$work/output.ppm" PPM 2 2
        same_pixels "$work/output.ppm" "$work/rgb.ppm" ;;
      pamtoqoi)
        exe=$(original_elf /usr/bin/pamtoqoi)
        "$exe" "$work/rgb.ppm" > "$work/output.qoi" 2> "$work/error"
        [ "$(busy head -c 4 "$work/output.qoi")" = qoif ]
        /usr/bin/qoitopam "$work/output.qoi" > "$work/decoded.pam"
        assert_image "$work/decoded.pam" PAM 2 2
        same_pixels "$work/decoded.pam" "$work/rgb.ppm" ;;
      qoitopam)
        exe=$(original_elf /usr/bin/qoitopam)
        /usr/bin/pamtoqoi "$work/rgb.ppm" > "$work/image.qoi"
        "$exe" "$work/image.qoi" > "$work/output.pam" 2> "$work/error"
        assert_image "$work/output.pam" PAM 2 2
        same_pixels "$work/output.pam" "$work/rgb.ppm" ;;
      pamtopng)
        exe=$(original_elf /usr/bin/pamtopng)
        "$exe" "$work/rgb.ppm" > "$work/output.png" 2> "$work/error"
        [ "$(busy dd if="$work/output.png" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')" = 89504e47 ]
        /usr/bin/pngtopam "$work/output.png" > "$work/decoded.pam"
        same_pixels "$work/decoded.pam" "$work/rgb.ppm" ;;
      pngtopnm)
        exe=$(original_elf /usr/bin/pngtopnm)
        /usr/bin/pamtopng "$work/rgb.ppm" > "$work/image.png"
        "$exe" "$work/image.png" > "$work/output.ppm" 2> "$work/error"
        assert_image "$work/output.ppm" PPM 2 2
        same_pixels "$work/output.ppm" "$work/rgb.ppm" ;;
      pbmtowbmp)
        exe=$(original_elf /usr/bin/pbmtowbmp)
        "$exe" "$work/mono.pbm" > "$work/output.wbmp" 2> "$work/error"
        [ -s "$work/output.wbmp" ]
        /usr/bin/wbmptopbm "$work/output.wbmp" > "$work/decoded.pbm"
        assert_image "$work/decoded.pbm" PBM 2 2
        same_pixels "$work/decoded.pbm" "$work/mono.pbm" ;;
      wbmptopbm)
        exe=$(original_elf /usr/bin/wbmptopbm)
        /usr/bin/pbmtowbmp "$work/mono.pbm" > "$work/image.wbmp"
        "$exe" "$work/image.wbmp" > "$work/output.pbm" 2> "$work/error"
        assert_image "$work/output.pbm" PBM 2 2
        same_pixels "$work/output.pbm" "$work/mono.pbm" ;;
      pnmenlarge)
        exe=$(original_elf /usr/bin/pnmenlarge)
        "$exe" 2 "$work/rgb.ppm" > "$work/output.ppm" 2> "$work/error"
        assert_image "$work/output.ppm" PPM 4 4 ;;
      pnmcrop)
        exe=$(original_elf /usr/bin/pnmcrop)
        "$exe" -white "$work/border.ppm" > "$work/output.ppm" 2> "$work/error"
        assert_image "$work/output.ppm" PPM 2 2
        same_pixels "$work/output.ppm" "$work/black.ppm" ;;
      pnmrotate)
        exe=$(original_elf /usr/bin/pnmrotate)
        "$exe" 90 "$work/rect.ppm" > "$work/output.ppm" 2> "$work/error"
        # Upstream's three-shear algorithm expands the containing rectangle
        # with antialiasing. Its precise rounded canvas is not always 2x3.
        meta "$work/output.ppm"
        busy awk '$2=="PPM" && $4>=2 && $5>=2 &&
                  ($4!=3 || $5!=2) {okay=1} END {exit !okay}' \
            "$work/metadata" ;;
      pnmgamma)
        exe=$(original_elf /usr/bin/pnmgamma)
        "$exe" 2.0 "$work/gray.pgm" > "$work/output.pgm" 2> "$work/error"
        assert_image "$work/output.pgm" PGM 2 2
        [ -s "$work/output.pgm" ]
        if same_pixels "$work/output.pgm" "$work/gray.pgm"; then return 1; fi ;;
      pnmcolormap)
        exe=$(original_elf /usr/bin/pnmcolormap)
        "$exe" 2 "$work/rgb.ppm" > "$work/output.ppm" 2> "$work/error"
        meta "$work/output.ppm"
        busy awk '$2=="PPM" && ($4*$5)==2 {okay=1} END {exit !okay}' \
            "$work/metadata" ;;
      pgmtopbm)
        exe=$(original_elf /usr/bin/pgmtopbm)
        "$exe" "$work/gray.pgm" > "$work/output.pbm" 2> "$work/error"
        assert_image "$work/output.pbm" PBM 2 2 ;;
      pgmnorm)
        exe=$(original_elf /usr/bin/pgmnorm)
        # On four pixels, percentile defaults round to 0..255 and perform
        # no change. Specify real measured source endpoints 30 and 220.
        "$exe" -bvalue=30 -wvalue=220 "$work/gray.pgm" \
            > "$work/output.pgm" 2> "$work/error"
        assert_image "$work/output.pgm" PGM 2 2
        if same_pixels "$work/output.pgm" "$work/gray.pgm"; then return 1; fi ;;
      *) echo "[M131 UNKNOWN ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M131_TRACE:-0}" = 1 ]; then
        PS4='[M131 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in pamtopam pamtopnm pamtoqoi qoitopam pamtopng pngtopnm \
           pbmtowbmp wbmptopbm pnmenlarge pnmcrop pnmrotate pnmgamma \
           pnmcolormap pgmtopbm pgmnorm; do
    echo "[M131 CASE START] $app"
    case_log="/tmp/m131-$app-case.log"
    if M131_TRACE=1 /bin/sh /tests/m131-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M131_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M131 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -100 "$case_log" >&2 || :
        for detail in error metadata output.pam output.ppm output.pbm \
                      output.pgm decoded.pam; do
            if [ -s "/tmp/m131-$app/$detail" ]; then
                echo "[M131 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 600 "/tmp/m131-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M131_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M131 BATCH FAIL] $failed/15 original ELFs (complete census)" >&2
    echo M131_BATCH_FAIL
    exit 1
fi
echo M131_BATCH_PASS
