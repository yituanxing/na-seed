#!/bin/sh
# Real image creation, manipulation, PICT/Windows ICO round trips and terminal
# encodings from 33 NEW signed upstream Netpbm executables. Each case is
# isolated, and only a successfully parsed nonempty result earns a PASS.
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
    # Real decoded dimensions and pixel values are parsed by original Netpbm.
    /usr/bin/pamcut -left 0 -top 0 -width 3 -height 3 "$image" \
        > "$work/crop.pnm" 2> "$work/crop-error" || return 1
    /usr/bin/pamtable "$work/crop.pnm" > "$work/pixels" 2> "$work/pixels-error" || return 1
    [ "$(busy wc -l < "$work/pixels")" -eq 3 ] || return 1
    busy grep -qE '[0-9]' "$work/pixels"
}
case_run() {
    app=$1
    exe=$(original "$app") || return 1
    work="/tmp/m163-$app"
    busy mkdir -p "$work" || return 1
    rgb=/tests/m163-color.ppm
    gray=/tests/m163-gray.pgm
    bw=/tests/m163-bw.pbm
    out="$work/output.pnm"
    case "$app" in
      pgmabel|pgmbentley|pgmdeshadow)
        "$exe" "$gray" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pgmkernel)
        "$exe" 3 3 > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pgmminkowski)
        "$exe" "$gray" > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/result")" -gt 100 ] &&
            busy grep -qE '[0-9]' "$work/result" ;;
      pgmnoise)
        "$exe" 16 16 > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pgmtopgm)
        "$exe" < "$gray" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmdist|ppmntsc|ppmrelief)
        "$exe" "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmcie)
        "$exe" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmcolormask)
        "$exe" black "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmdim|ppmflash)
        "$exe" 0.5 "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmforge)
        "$exe" -clouds -width 32 -height 32 > "$out" 2> "$work/error" &&
            image_check "$out" "$work" ;;
      ppmglobe)
        "$exe" 2 "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmhist)
        "$exe" "$rgb" > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/result")" -gt 200 ] &&
            busy grep -qE '[1-9]' "$work/result" ;;
      ppmmix)
        "$exe" 0.5 "$rgb" "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmpat)
        "$exe" -gingham2 32 32 > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmrough)
        "$exe" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmwheel)
        "$exe" 32 > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmtopuzz)
        "$exe" "$rgb" > "$work/puzz.bin" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/puzz.bin")" -gt 200 ] &&
            ! busy cmp -s "$rgb" "$work/puzz.bin" ;;
      pbmtoescp2)
        "$exe" "$bw" > "$work/printer.bin" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/printer.bin")" -gt 24 ] || return 1
        sig=$(busy dd if="$work/printer.bin" bs=1 count=2 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
        [ "$sig" = 1b28 ] ;;
      pnmstitch)
        "$exe" "$rgb" "$rgb" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pbmminkowski)
        "$exe" "$bw" > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/result")" -gt 20 ] &&
            busy grep -qE '[0-9]' "$work/result" ;;
      pbmpscale)
        "$exe" 2 "$bw" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmtoterm|ppmtosixel|ppmtoicr)
        "$exe" "$rgb" > "$work/terminal" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/terminal")" -gt 128 ] &&
            ! busy cmp -s "$rgb" "$work/terminal" ;;
      ppmtopict)
        "$exe" "$rgb" > "$work/pict.bin" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/pict.bin")" -gt 500 ] || return 1
        /usr/bin/picttoppm "$work/pict.bin" > "$out" 2> "$work/roundtrip-error" &&
            image_check "$out" "$work" ;;
      picttoppm)
        /usr/bin/ppmtopict "$rgb" > "$work/pict.bin" 2> "$work/encode-error" || return 1
        "$exe" "$work/pict.bin" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      ppmtowinicon)
        "$exe" "$rgb" > "$work/icon.ico" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/icon.ico")" -gt 500 ] || return 1
        /usr/bin/winicontoppm "$work/icon.ico" > "$out" 2> "$work/roundtrip-error" &&
            image_check "$out" "$work" ;;
      winicontoppm)
        /usr/bin/ppmtowinicon "$rgb" > "$work/icon.ico" 2> "$work/encode-error" || return 1
        "$exe" "$work/icon.ico" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      *) echo "[M163 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in pgmabel pgmbentley pgmdeshadow pgmkernel pgmminkowski pgmnoise \
           pgmtopgm ppmtopuzz ppmcie ppmcolormask ppmdim ppmdist ppmflash \
           ppmforge ppmglobe ppmhist ppmmix ppmntsc ppmpat ppmrelief ppmrough \
           ppmwheel pbmtoescp2 pnmstitch pbmminkowski pbmpscale ppmtoterm \
           ppmtosixel ppmtoicr ppmtopict picttoppm ppmtowinicon winicontoppm; do
    echo "[M163 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m163-$app-case.log"
    if /bin/sh /tests/m163-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M163_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M163 GENUINE SOFTWARE FAIL] $app rc=$rc" >&2
        busy tail -30 "$detail" >&2 || :
        for stderr in /tmp/m163-$app/error /tmp/m163-$app/metadata-error \
                      /tmp/m163-$app/pixels-error /tmp/m163-$app/roundtrip-error; do
            [ -s "$stderr" ] && busy head -c 460 "$stderr" >&2 || :
        done
        printf 'M163_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M163 BATCH FAIL] $failures/33 actual signed ELF operations" >&2
    echo M163_BATCH_FAIL
    exit 1
fi
echo M163_BATCH_PASS
