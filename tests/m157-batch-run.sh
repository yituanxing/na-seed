#!/bin/sh
# Original signed Alpine image processors run on real data, not --help.
# Each case opens an actual fixture and is required to emit a parseable image,
# text pixel table, modified document, or byte-exact repaired file.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
original() {
    exe="/usr/bin/$1"
    [ -x "$exe" ] || return 1
    resolved=$(busy readlink -f "$exe") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$exe"
}
pnm() {
    [ -s "$work/out.pnm" ] || return 1
    magic=$(busy dd if="$work/out.pnm" bs=1 count=2 2>/dev/null)
    case "$magic" in P[1-7]) ;; *) return 1 ;; esac
    /usr/bin/pamfile "$work/out.pnm" > "$work/check" 2>&1 || return 1
    busy grep -qE '[0-9]+ by [0-9]+' "$work/check"
}
png() {
    [ -s "$work/out.png" ] || return 1
    sig=$(busy dd if="$work/out.png" bs=1 count=8 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$sig" = 89504e470d0a1a0a ] || return 1
    /usr/bin/pngtopam "$work/out.png" > "$work/decoded.pam" 2> "$work/check" || return 1
    [ -s "$work/decoded.pam" ]
}
gif() {
    [ -s "$work/out.gif" ] || return 1
    busy head -c 6 "$work/out.gif" | busy grep -qE '^GIF8[79]a$' || return 1
    /usr/bin/giftext -p "$work/out.gif" > "$work/decoded.txt" 2> "$work/check" || return 1
    [ -s "$work/decoded.txt" ]
}
tiff() {
    [ -s "$work/out.tif" ] || return 1
    sig=$(busy dd if="$work/out.tif" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    case "$sig" in 49492a00|4d4d002a) ;; *) return 1 ;; esac
    /usr/bin/tiffinfo "$work/out.tif" > "$work/check" 2>&1 || return 1
    busy grep -q 'Image Width:' "$work/check"
}
case_run() {
    app=$1
    work="/tmp/m157-$app"
    busy mkdir -p "$work"
    exe=$(original "$app") || return 1
    img=/tests/m157-color.ppm
    gray=/tests/m157-gray.pgm
    mask=/tests/m157-bw.pbm
    case "$app" in
      pamaddnoise) "$exe" -type gaussian -seed 157 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamaltsat) "$exe" -method log -strength 1.2 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pambrighten) "$exe" -value=10 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamgetcolor)
        "$exe" -infile "$img" 0,0:top 7,7:bottom > "$work/result" 2> "$work/error" || return 1
        busy grep -q top "$work/result" && busy grep -q bottom "$work/result" ;;
      pamhue) "$exe" -huechange=30 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamlevels)
        # Alpine's signed Netpbm package does not ship the optional X11
        # rgb.txt color-name dictionary. Numeric colors test the real levels
        # transform without adding a synthetic host file to the guest.
        "$exe" -from1 rgb-255:0/0/0 -to1 rgb-255:0/0/0 \
               -from2 rgb-255:255/255/255 -to2 rgb-255:255/255/255 \
               "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamrestack) "$exe" -width=16 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamslice)
        "$exe" -row=3 "$img" > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -l < "$work/result")" -ge 8 ] ;;
      pamsummcol) "$exe" -mean "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pamtable)
        "$exe" "$img" > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -l < "$work/result")" -ge 8 ] ;;
      pbmlife) "$exe" "$mask" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pbmmask) "$exe" "$mask" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pgmenhance) "$exe" "$gray" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pgmramp) "$exe" -lr 8 8 > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pnmalias) "$exe" "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pnmnlfilt) "$exe" 0.5 1.0 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pnmremap) "$exe" -mapfile="$img" "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      pnmscalefixed) "$exe" 2 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      ppmshift) "$exe" 2 "$img" > "$work/out.pnm" 2> "$work/error" && pnm ;;
      ppmspread) "$exe" 1 "$img" -randomseed=157 > "$work/out.pnm" 2> "$work/error" && pnm ;;
      gifbuild)
        "$exe" -d /tests/m157-color.gif > "$work/result" 2> "$work/error" || return 1
        busy grep -q 'screen width' "$work/result" &&
        "$exe" < "$work/result" > "$work/out.gif" 2> "$work/error" && gif ;;
      gifclrmp)
        "$exe" -s /tests/m157-color.gif > "$work/result" 2> "$work/error" || return 1
        [ "$(busy wc -l < "$work/result")" -ge 2 ] ;;
      giffix) "$exe" /tests/m157-color.gif > "$work/out.gif" 2> "$work/error" && gif ;;
      giftext)
        "$exe" -p /tests/m157-color.gif > "$work/result" 2> "$work/error" || return 1
        [ -s "$work/result" ] && busy grep -iq 'image' "$work/result" ;;
      giftool)
        "$exe" -b 1 < /tests/m157-color.gif > "$work/out.gif" 2> "$work/error" && gif &&
        ! busy cmp -s /tests/m157-color.gif "$work/out.gif" ;;
      fax2ps)
        /usr/bin/fax2tiff -U -X 8 -o "$work/fax.tif" /tests/m157-fax.raw > "$work/prep" 2> "$work/prep-error" || return 1
        "$exe" "$work/fax.tif" > "$work/out.ps" 2> "$work/error" || return 1
        busy grep -q '%!PS' "$work/out.ps" && busy grep -q 'showpage' "$work/out.ps" ;;
      fax2tiff) "$exe" -U -X 8 -o "$work/out.tif" /tests/m157-fax.raw > "$work/result" 2> "$work/error" && tiff ;;
      pal2rgb) "$exe" /tests/m157-paletted.tif "$work/out.tif" > "$work/result" 2> "$work/error" && tiff ;;
      pngfix)
        "$exe" /tests/m157-color.png > "$work/result" 2> "$work/error" || return 1
        [ -s "$work/result" ] || [ -s "$work/error" ] ;;
      png-fix-itxt)
        "$exe" < /tests/m157-bad-itxt.png > "$work/out.png" 2> "$work/error" || return 1
        png && busy cmp -s /tests/m157-color.png "$work/out.png" ;;
      *) echo "[M157 UNKNOWN PROGRAM] $app" >&2; return 2 ;;
    esac
}
if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in pamaddnoise pamaltsat pambrighten pamgetcolor pamhue pamlevels \
           pamrestack pamslice pamsummcol pamtable pbmlife pbmmask \
           pgmenhance pgmramp pnmalias pnmnlfilt pnmremap pnmscalefixed \
           ppmshift ppmspread gifbuild gifclrmp giffix giftext giftool \
           fax2ps fax2tiff pal2rgb pngfix png-fix-itxt; do
    echo "[M157 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m157-$app-case.log"
    if /bin/sh /tests/m157-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M157_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M157 ORIGINAL FUNCTION FAIL] $app rc=$rc" >&2
        busy tail -30 "$detail" >&2 || :
        if [ -s "/tmp/m157-$app/error" ]; then
            busy head -c 400 "/tmp/m157-$app/error" >&2 || :
        fi
        printf 'M157_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M157 BATCH FAIL] $failures/30 genuine image operations" >&2
    echo M157_BATCH_FAIL
    exit 1
fi
echo M157_BATCH_PASS
