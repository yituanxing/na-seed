#!/bin/sh
# Thirty entirely NEW unmodified signed Alpine Netpbm ELFs. Every program
# manipulates real PNM pixels, analyzes coordinates, generates a real device
# stream or participates in JPEG2000/SBIG/JBIG encode+decode round trips.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
RGBDEF=/tests/m163-rgb.txt
export PATH HOME LC_ALL RGBDEF
busy() { /bin/busybox "$@"; }
original() {
    exe="/usr/bin/$1"
    [ -x "$exe" ] || return 1
    resolved=$(busy readlink -f "$exe") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$exe"
}
image_check() {
    image=$1
    work=$2
    [ -s "$image" ] || return 1
    magic=$(busy dd if="$image" bs=1 count=2 2>/dev/null)
    case "$magic" in P[1-7]) ;; *) return 1 ;; esac
    /usr/bin/pamfile "$image" > "$work/metadata" 2> "$work/metadata-error" || return 1
    busy grep -Eq '[0-9]+ by [0-9]+' "$work/metadata" || return 1
    /usr/bin/pamcut -left 0 -top 0 -width 3 -height 3 "$image" \
        > "$work/crop.pnm" 2> "$work/crop-error" || return 1
    /usr/bin/pamtable "$work/crop.pnm" > "$work/pixels" 2> "$work/pixels-error" || return 1
    [ "$(busy wc -l < "$work/pixels")" -eq 3 ] || return 1
    busy grep -qE '[0-9]' "$work/pixels"
}
encoded_check() {
    data=$1
    minimum=$2
    source=$3
    [ -s "$data" ] && [ "$(busy wc -c < "$data")" -ge "$minimum" ] &&
        ! busy cmp -s "$source" "$data"
}
case_run() {
    app=$1
    exe=$(original "$app") || return 1
    work="/tmp/m165-$app"
    busy mkdir -p "$work" || return 1
    color=/tests/m163-color.ppm
    gray=/tests/m163-gray.pgm
    bw=/tests/m163-bw.pbm
    out="$work/output.pnm"
    case "$app" in
      pamshadedrelief)
        "$exe" "$gray" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pammosaicknit|pammixinterlace|pammixmulti|pampaintspill|pamsistoaglyph|ppmlabel)
        "$exe" "$color" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pamwipeout)
        "$exe" -lr "$color" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppm3d)
        "$exe" "$color" "$color" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmtv)
        "$exe" 0.5 "$color" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pamperspective)
        "$exe" -ulx 0 -uly 0 -urx 8 -ury 0 -llx 0 -lly 8 \
            -lrx 8 -lry 8 "$color" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pamsumm)
        "$exe" -mean "$color" > "$work/result" 2> "$work/error" || return 1
        encoded_check "$work/result" 20 "$color" &&
            busy grep -qE '[0-9]' "$work/result" ;;
      pamfind)
        "$exe" -color black "$color" > "$work/result" 2> "$work/error" || return 1
        encoded_check "$work/result" 20 "$color" &&
            busy grep -qE '[0-9]' "$work/result" ;;
      ppmtoacad)
        "$exe" "$color" > "$work/output.acad" 2> "$work/error" || return 1
        encoded_check "$work/output.acad" 200 "$color" &&
            [ "$(busy head -c 3 "$work/output.acad")" = Aut ] ;;
      pbmto4425|pbmtolj|pbmtopi3|pbmtoptx|pbmto10x|pbmtonokia|pbmtozinc|pbmtoepson)
        "$exe" "$bw" > "$work/printer.bin" 2> "$work/error" || return 1
        encoded_check "$work/printer.bin" 15 "$bw" ;;
      ppmtopjxl)
        "$exe" "$color" > "$work/pjxl.bin" 2> "$work/error" || return 1
        encoded_check "$work/pjxl.bin" 100 "$color" ;;
      ppmtoeyuv)
        "$exe" "$color" > "$work/yuv.bin" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/yuv.bin")" -eq 96 ] ;;
      pamtojpeg2k)
        "$exe" "$color" > "$work/data.jp2" 2> "$work/error" || return 1
        encoded_check "$work/data.jp2" 200 "$color" || return 1
        /usr/bin/jpeg2ktopam "$work/data.jp2" > "$out" 2> "$work/roundtrip-error" &&
            image_check "$out" "$work" ;;
      jpeg2ktopam)
        /usr/bin/pamtojpeg2k "$color" > "$work/data.jp2" 2> "$work/encode-error" || return 1
        "$exe" "$work/data.jp2" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pgmtosbig)
        "$exe" "$gray" > "$work/data.sbig" 2> "$work/error" || return 1
        encoded_check "$work/data.sbig" 200 "$gray" || return 1
        /usr/bin/sbigtopgm "$work/data.sbig" > "$out" 2> "$work/roundtrip-error" &&
            image_check "$out" "$work" ;;
      sbigtopgm)
        /usr/bin/pgmtosbig "$gray" > "$work/data.sbig" 2> "$work/encode-error" || return 1
        "$exe" "$work/data.sbig" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pnmtojbig)
        "$exe" "$bw" > "$work/data.jbig" 2> "$work/error" || return 1
        encoded_check "$work/data.jbig" 20 "$bw" || return 1
        /usr/bin/jbigtopnm "$work/data.jbig" > "$out" 2> "$work/roundtrip-error" &&
            image_check "$out" "$work" ;;
      jbigtopnm)
        /usr/bin/pnmtojbig "$bw" > "$work/data.jbig" 2> "$work/encode-error" || return 1
        "$exe" "$work/data.jbig" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      *) echo "[M165 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in pamshadedrelief pammosaicknit pammixinterlace pammixmulti \
           pampaintspill pamsistoaglyph ppmlabel ppmtoacad pbmto4425 \
           pbmtolj pbmtopi3 pbmtoptx pbmto10x pbmtonokia pbmtozinc \
           pamtojpeg2k pgmtosbig ppmtopjxl ppmtoeyuv pbmtoepson \
           pamsumm pamwipeout pamfind ppm3d ppmtv pamperspective \
           jpeg2ktopam sbigtopgm pnmtojbig jbigtopnm; do
    echo "[M165 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m165-$app-case.log"
    if /bin/sh /tests/m165-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M165_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M165 GENUINE CODEC FAIL] $app rc=$rc" >&2
        busy tail -30 "$detail" >&2 || :
        for stderr in /tmp/m165-$app/error /tmp/m165-$app/metadata-error \
                      /tmp/m165-$app/roundtrip-error /tmp/m165-$app/encode-error; do
            [ -s "$stderr" ] && busy head -c 450 "$stderr" >&2 || :
        done
        printf 'M165_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M165 BATCH FAIL] $failures/30 actual signed image operations" >&2
    echo M165_BATCH_FAIL
    exit 1
fi
echo M165_BATCH_PASS
