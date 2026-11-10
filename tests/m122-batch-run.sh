#!/bin/sh
# Fifteen distinct SIGNED ORIGINAL Linux Netpbm executables, semantic oracles.
# Every PNM/PNG fixture and golden pixel image originates on the Linux host.
# NaSeed guest runs executable code only; no guest compilation or fixture build.
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
    echo "[M122 MISSING SIGNED ORIGINAL ELF] $*" >&2
    return 1
}

check_input() {
    for fixture in original.ppm red.ppm blue.ppm black.pbm white.pgm \
                   flipped.ppm cropped.ppm inverted.ppm red-channel.pgm \
                   red8.ppm red31.ppm red-blue.ppm original.png invalid.ppm; do
        [ -s "/tests/m122-$fixture" ] || return 1
    done
    magic=$(busy dd if=/tests/m122-original.png bs=1 count=8 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 89504e470d0a1a0a ]
}

assert_meta() {
    # Upstream pamfile -machine: "filename: FORMAT RAW W H DEPTH MAXVAL TUPLE".
    file=$1; format=$2; width=$3; height=$4; channels=$5; max=$6
    probe=$(original_elf /usr/bin/pamfile)
    "$probe" -machine "$file" > "$work/metadata"
    busy awk -v fmt="$format" -v w="$width" -v h="$height" \
             -v depth="$channels" -v limit="$max" '
        $2 == fmt && $4 == w && $5 == h && $6 == depth && $7 == limit { seen=1 }
        END { exit !seen }
    ' "$work/metadata"
}

assert_same_pixels() {
    # Convert both to upstream plain PNM before byte-for-byte comparison: this
    # makes the oracle independent of RAW/PLAIN and header line wrapping.
    actual=$1; golden=$2
    canonical=$(original_elf /usr/bin/pamtopnm)
    "$canonical" -plain "$actual" > "$work/actual-plain.pnm"
    "$canonical" -plain "$golden" > "$work/golden-plain.pnm"
    busy cmp "$work/actual-plain.pnm" "$work/golden-plain.pnm"
}

assert_png() {
    [ -s "$1" ]
    sig=$(busy dd if="$1" bs=1 count=8 2>/dev/null |
          busy od -An -tx1 | busy tr -d ' \n')
    [ "$sig" = 89504e470d0a1a0a ]
}

run_case() {
    app=$1
    work="/tmp/m122-$app"
    busy mkdir -p "$work"
    rgb=/tests/m122-original.ppm
    red=/tests/m122-red.ppm
    blue=/tests/m122-blue.ppm
    case "$app" in
      pbmmake)
        exe=$(original_elf /usr/bin/pbmmake)
        "$exe" -black 4 4 > "$work/black.pbm"
        assert_meta "$work/black.pbm" PBM 4 4 1 1
        assert_same_pixels "$work/black.pbm" /tests/m122-black.pbm ;;
      pgmmake)
        exe=$(original_elf /usr/bin/pgmmake)
        "$exe" 1 4 4 > "$work/white.pgm"
        assert_meta "$work/white.pgm" PGM 4 4 1 255
        assert_same_pixels "$work/white.pgm" /tests/m122-white.pgm ;;
      ppmmake)
        exe=$(original_elf /usr/bin/ppmmake)
        # Numeric RGB is independent of optional X11 rgb.txt name dictionaries.
        "$exe" '#ff0000' 4 4 > "$work/red.ppm"
        assert_meta "$work/red.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/red.ppm" "$red" ;;
      pamfile)
        exe=$(original_elf /usr/bin/pamfile)
        "$exe" -machine "$rgb" > "$work/info"
        busy awk '$2 == "PPM" && $4 == 4 && $5 == 4 && $6 == 3 && $7 == 255 { ok=1 } END {exit !ok}' "$work/info"
        "$exe" -size "$rgb" > "$work/size"
        busy grep -Fxq '4 4' "$work/size" ;;
      pamvalidate)
        exe=$(original_elf /usr/bin/pamvalidate)
        "$exe" < "$rgb" > "$work/verified.ppm"
        busy cmp "$rgb" "$work/verified.ppm"
        if "$exe" < /tests/m122-invalid.ppm > "$work/invalid-out" 2> "$work/invalid-error"; then
            echo '[M122 VALIDATE INVALID] truncated raster accepted' >&2
            return 1
        fi
        [ ! -s "$work/invalid-out" ] ;;
      pamflip)
        exe=$(original_elf /usr/bin/pamflip)
        "$exe" -leftright "$rgb" > "$work/flipped.ppm"
        assert_meta "$work/flipped.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/flipped.ppm" /tests/m122-flipped.ppm ;;
      pamcut)
        exe=$(original_elf /usr/bin/pamcut)
        "$exe" -left 1 -top 1 -width 2 -height 2 "$rgb" > "$work/crop.ppm"
        assert_meta "$work/crop.ppm" PPM 2 2 3 255
        assert_same_pixels "$work/crop.ppm" /tests/m122-cropped.ppm ;;
      pamscale)
        exe=$(original_elf /usr/bin/pamscale)
        "$exe" -xysize 8 8 "$red" > "$work/large.ppm"
        assert_meta "$work/large.ppm" PPM 8 8 3 255
        assert_same_pixels "$work/large.ppm" /tests/m122-red8.ppm ;;
      pamchannel)
        exe=$(original_elf /usr/bin/pamchannel)
        # Need a recognized tuple type for pamtopnm conversion.
        "$exe" -infile "$rgb" -tupletype GRAYSCALE 0 > "$work/channel.pam"
        assert_meta "$work/channel.pam" PAM 4 4 1 255
        assert_same_pixels "$work/channel.pam" /tests/m122-red-channel.pgm ;;
      pamdepth)
        exe=$(original_elf /usr/bin/pamdepth)
        "$exe" 31 "$red" > "$work/red31.ppm"
        assert_meta "$work/red31.ppm" PPM 4 4 3 31
        assert_same_pixels "$work/red31.ppm" /tests/m122-red31.ppm ;;
      pamcat)
        exe=$(original_elf /usr/bin/pamcat)
        "$exe" -leftright "$red" "$blue" > "$work/red-blue.ppm"
        assert_meta "$work/red-blue.ppm" PPM 8 4 3 255
        assert_same_pixels "$work/red-blue.ppm" /tests/m122-red-blue.ppm ;;
      pnminvert)
        exe=$(original_elf /usr/bin/pnminvert)
        "$exe" "$rgb" > "$work/negative.ppm"
        assert_meta "$work/negative.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/negative.ppm" /tests/m122-inverted.ppm ;;
      pnmtopng)
        exe=$(original_elf /usr/bin/pnmtopng)
        "$exe" "$rgb" > "$work/encoded.png"
        assert_png "$work/encoded.png"
        decoder=$(original_elf /usr/bin/pngtopam)
        "$decoder" "$work/encoded.png" > "$work/roundtrip.ppm"
        assert_meta "$work/roundtrip.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/roundtrip.ppm" "$rgb" ;;
      pngtopam)
        exe=$(original_elf /usr/bin/pngtopam)
        "$exe" /tests/m122-original.png > "$work/decoded.ppm"
        assert_meta "$work/decoded.ppm" PPM 4 4 3 255
        assert_same_pixels "$work/decoded.ppm" "$rgb" ;;
      pnmtojpeg)
        exe=$(original_elf /usr/bin/pnmtojpeg)
        "$exe" -quality=100 "$red" > "$work/red.jpg"
        sig=$(busy dd if="$work/red.jpg" bs=1 count=2 2>/dev/null |
              busy od -An -tx1 | busy tr -d ' \n')
        [ "$sig" = ffd8 ]
        decoder=$(original_elf /usr/bin/jpegtopnm)
        "$decoder" "$work/red.jpg" > "$work/recovered.ppm"
        assert_meta "$work/recovered.ppm" PPM 4 4 3 255
        canonical=$(original_elf /usr/bin/pamtopnm)
        "$canonical" -plain "$work/recovered.ppm" > "$work/recovered.p3"
        # JPEG is lossy. Assert all 16 pixels remain red-dominant, not equality.
        busy awk '
            NR > 3 {
                for (i=1; i<=NF; i++) {
                    n++; channel=(n-1)%3;
                    if (channel==0 && $i<200) bad=1;
                    if (channel!=0 && $i>55) bad=1;
                }
            }
            END { exit (bad || n!=48) }
        ' "$work/recovered.p3" ;;
      *) echo "[M122 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M122_TRACE:-0}" = 1 ]; then
        PS4='[M122 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m122-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in pbmmake pgmmake ppmmake pamfile pamvalidate pamflip pamcut \
           pamscale pamchannel pamdepth pamcat pnminvert pnmtopng pngtopam \
           pnmtojpeg; do
    echo "[M122 CASE START] $app"
    case_log="/tmp/m122-$app-case.log"
    if M122_TRACE=1 /bin/sh /tests/m122-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M122_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M122 $upper FAILED] signed original Netpbm Linux ELF, rc=$rc" >&2
        busy tail -115 "$case_log" >&2
        printf 'M122_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M122_BATCH_PASS
else
    echo "[M122 BATCH FAIL] $failed/15 original Linux Netpbm applications" >&2
    echo M122_BATCH_FAIL
    exit 1
fi
