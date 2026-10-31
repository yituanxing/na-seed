#!/bin/sh
# Fifteen NEW genuine Alpine Netpbm ELF binaries, each with a semantic oracle.
# Linux host pre-generates all fixtures; NaSeed executes images, never builds.
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
    echo "[M125 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

check_input() {
    for fixture in pattern.ppm red.ppm blue.ppm magenta.ppm black.ppm \
                   cyan.ppm checker.ppm bw-checker.ppm bw-checker.pgm \
                   gray.pgm gray-to-red.ppm threshold.pbm indices.pgm \
                   lookup.ppm odd.ppm ascii.txt tile_0_0.ppm tile_0_1.ppm \
                   tile_1_0.ppm tile_1_1.ppm; do
        [ -s "/tests/m125-$fixture" ] || return 1
    done
}

assert_meta() {
    image=$1; fmt=$2; width=$3; height=$4; channels=$5; maxval=$6
    probe=$(original_elf /usr/bin/pamfile)
    "$probe" -machine "$image" > "$work/metadata"
    busy awk -v fmt="$fmt" -v w="$width" -v h="$height" \
             -v depth="$channels" -v lim="$maxval" '
        $2==fmt && $4==w && $5==h && $6==depth && $7==lim {found=1}
        END {exit !found}
    ' "$work/metadata"
}

assert_same_pixels() {
    # The original pamtopnm normalizes P4/P5/P6/P7 and line wrapping to
    # a common plain representation; the expected pixel raster is generated
    # independently by Python on the Linux host (never in the NaSeed guest).
    normalizer=$(original_elf /usr/bin/pamtopnm)
    "$normalizer" -plain "$1" > "$work/actual-plain.pnm"
    "$normalizer" -plain "$2" > "$work/expected-plain.pnm"
    busy cmp -s "$work/actual-plain.pnm" "$work/expected-plain.pnm"
}

run_case() {
    app=$1
    work="/tmp/m125-$app"
    busy mkdir -p "$work"
    pattern=/tests/m125-pattern.ppm
    red=/tests/m125-red.ppm
    blue=/tests/m125-blue.ppm
    gray=/tests/m125-gray.pgm
    case "$app" in
      asciitopgm)
        exe=$(original_elf /usr/bin/asciitopgm)
        "$exe" 2 4 /tests/m125-ascii.txt > "$work/output.pgm"
        assert_meta "$work/output.pgm" PGM 4 2 1 127
        normalizer=$(original_elf /usr/bin/pamtopnm)
        "$normalizer" -plain "$work/output.pgm" > "$work/out.p2"
        busy awk 'NR>3 {for(i=1;i<=NF;i++) a[++n]=$i}
                 END {exit !(n==8 && a[1]==127 && a[2]==127 &&
                               a[3]==127 && a[4]==127 && a[5]<127 &&
                               a[5]==a[6] && a[6]==a[7] && a[7]==a[8])}' \
                 "$work/out.p2" ;;
      pamcomp)
        exe=$(original_elf /usr/bin/pamcomp)
        "$exe" "$blue" "$red" > "$work/combined.ppm"
        assert_meta "$work/combined.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/combined.ppm" "$blue" ;;
      pamarith)
        exe=$(original_elf /usr/bin/pamarith)
        "$exe" -add "$red" "$blue" > "$work/sum.ppm"
        assert_same_pixels "$work/sum.ppm" /tests/m125-magenta.ppm ;;
      pamfunc)
        exe=$(original_elf /usr/bin/pamfunc)
        "$exe" -xormask=ff "$red" > "$work/xor.ppm"
        assert_same_pixels "$work/xor.ppm" /tests/m125-cyan.ppm ;;
      pamdice)
        exe=$(original_elf /usr/bin/pamdice)
        "$exe" -outstem="$work/tile" -width=2 -height=2 "$pattern"
        for y in 0 1; do for x in 0 1; do
            piece="$work/tile_${y}_${x}.ppm"
            assert_meta "$piece" PPM 2 2 3 255
            assert_same_pixels "$piece" "/tests/m125-tile_${y}_${x}.ppm"
        done; done ;;
      pamundice)
        exe=$(original_elf /usr/bin/pamundice)
        "$exe" -across=2 -down=2 '/tests/m125-tile_%1d_%1a.ppm' > "$work/rejoined.ppm"
        assert_meta "$work/rejoined.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/rejoined.ppm" "$pattern" ;;
      ppmtopgm)
        exe=$(original_elf /usr/bin/ppmtopgm)
        "$exe" /tests/m125-bw-checker.ppm > "$work/gray.pgm"
        assert_meta "$work/gray.pgm" PGM 4 4 1 255
        assert_same_pixels "$work/gray.pgm" /tests/m125-bw-checker.pgm ;;
      pgmtoppm)
        exe=$(original_elf /usr/bin/pgmtoppm)
        "$exe" -black='#000000' -white='#ff0000' "$gray" > "$work/red-ramp.ppm"
        assert_meta "$work/red-ramp.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/red-ramp.ppm" /tests/m125-gray-to-red.ppm ;;
      ppmchange)
        exe=$(original_elf /usr/bin/ppmchange)
        "$exe" '#ff0000' '#0000ff' "$red" > "$work/recolored.ppm"
        assert_same_pixels "$work/recolored.ppm" "$blue" ;;
      pamthreshold)
        exe=$(original_elf /usr/bin/pamthreshold)
        "$exe" -simple -threshold=0.5 "$gray" > "$work/threshold.pam"
        assert_same_pixels "$work/threshold.pam" /tests/m125-threshold.pbm ;;
      pamdeinterlace)
        exe=$(original_elf /usr/bin/pamdeinterlace)
        "$exe" -takeodd "$pattern" > "$work/odd.ppm"
        assert_meta "$work/odd.ppm" PPM 4 2 3 255
        assert_same_pixels "$work/odd.ppm" /tests/m125-odd.ppm ;;
      pamedge)
        exe=$(original_elf /usr/bin/pamedge)
        "$exe" "$red" > "$work/edges.ppm"
        assert_meta "$work/edges.ppm" PPM 4 4 3 255
        # The Sobel gradient of a spatially uniform color must be zero.
        assert_same_pixels "$work/edges.ppm" /tests/m125-black.ppm ;;
      pamgauss)
        exe=$(original_elf /usr/bin/pamgauss)
        "$exe" 7 7 -sigma=.5 -maxval=255 -maximize -tupletype=GRAYSCALE > "$work/gauss.pam"
        assert_meta "$work/gauss.pam" PAM 7 7 1 255
        normalizer=$(original_elf /usr/bin/pamtopnm)
        "$normalizer" -plain "$work/gauss.pam" > "$work/gauss.p2"
        busy awk 'NR>3 {for(i=1;i<=NF;i++) a[++n]=$i}
                 END {exit !(n==49 && a[25]==255 && a[1]<a[25] &&
                              a[1]==a[7] && a[1]==a[43] && a[1]==a[49])}' \
                 "$work/gauss.p2" ;;
      pamgradient)
        exe=$(original_elf /usr/bin/pamgradient)
        "$exe" '#000000' '#ffffff' '#000000' '#ffffff' 4 4 > "$work/gradient.pam"
        assert_meta "$work/gradient.pam" PAM 4 4 1 255
        normalizer=$(original_elf /usr/bin/pamtopnm)
        "$normalizer" -plain "$work/gradient.pam" > "$work/gradient.p2"
        # Gradient intentionally does not hit the nominal right corner
        # exactly (upstream documents pixel-center interpolation).
        busy awk 'NR>3 {for(i=1;i<=NF;i++) a[++n]=$i}
                 END {exit !(n==16 && a[1]==0 && a[1]<a[2] &&
                              a[2]<a[3] && a[3]<a[4] && a[4]<255 &&
                              a[1]==a[13] && a[4]==a[16])}' \
                 "$work/gradient.p2" ;;
      pamlookup)
        exe=$(original_elf /usr/bin/pamlookup)
        "$exe" -lookupfile=/tests/m125-lookup.ppm /tests/m125-indices.pgm > "$work/lookup.ppm"
        assert_meta "$work/lookup.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/lookup.ppm" /tests/m125-checker.ppm ;;
      *) echo "[M125 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M125_TRACE:-0}" = 1 ]; then
        PS4='[M125 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m125-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in asciitopgm pamcomp pamarith pamfunc pamdice pamundice \
           ppmtopgm pgmtoppm ppmchange pamthreshold pamdeinterlace \
           pamedge pamgauss pamgradient pamlookup; do
    echo "[M125 CASE START] $app"
    case_log="/tmp/m125-$app-case.log"
    if M125_TRACE=1 /bin/sh /tests/m125-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M125_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M125 $upper FAILED] true native Linux ELF, rc=$rc" >&2
        busy tail -135 "$case_log" >&2
        printf 'M125_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M125_BATCH_PASS
else
    echo "[M125 BATCH FAIL] $failed/15 original Netpbm programs" >&2
    echo M125_BATCH_FAIL
    exit 1
fi
