#!/bin/sh
# M158: fifteen reversible, real-data format pairs = thirty NEW original ELFs.
# Every --case launches its particular target ELF and proves its binary image
# is decoded to meaningful pixels with the documented output dimensions.
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
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$exe"
}
hexprefix() {
    busy dd if="$1" bs=1 count="$2" 2>/dev/null |
        busy od -An -tx1 | busy tr -d ' \n'
}
format_check() {
    encoder=$1
    encoded=$2
    [ -s "$encoded" ] && [ "$(busy wc -c < "$encoded")" -ge 64 ] || return 1
    # Check the *actual* target encoding, not just a nonempty stdout file.
    # Three old bitmap/video formats have no identifying magic, so they have
    # exact fixed-plane byte lengths and additionally must roundtrip below.
    case "$encoder" in
      ppmtopcx)       [ "$(hexprefix "$encoded" 1)" = 0a ] ;;
      ppmtoilbm)      [ "$(hexprefix "$encoded" 4)" = 464f524d ] ;;
      ppmtoleaf)      [ "$(hexprefix "$encoded" 4)" = 894f5053 ] ;;
      ppmtoneo)       [ "$(busy wc -c < "$encoded")" -eq 32128 ] ;;
      ppmtopj)        [ "$(hexprefix "$encoded" 2)" = 1b45 ] ;;
      ppmtopi1)       [ "$(busy wc -c < "$encoded")" -eq 32034 ] ;;
      pgmtofs)        [ "$(hexprefix "$encoded" 10)" = 46697273744e616d653a ] ;;
      pgmtolispm)     [ "$(hexprefix "$encoded" 16)" = 546869732069732061204269744d6170 ] ;;
      pbmtoicon)      [ "$(hexprefix "$encoded" 16)" = 2f2a20466f726d61745f76657273696f ] ;;
      pbmtomacp)      [ "$(busy wc -c < "$encoded")" -eq 1992 ] ;;
      pnmtoxwd)
        # XWD has no 4-byte magic. Its first word is the XWD header size,
        # which includes the source filename: e.g. 0xA3 for a long host
        # path but a smaller value under the guest's /tests path. Validate
        # the actual 25-word XWD header's version, ZPixmap, pixel depth
        # and dimensions without hard-coding that variable first word.
        header=$(hexprefix "$encoded" 24)
        case "$header" in
          ????????0000000700000002000000080000000800000008) ;;
          *) return 1 ;;
        esac
        header_octets=$(busy dd if="$encoded" bs=1 count=4 2>/dev/null |
                        busy od -An -tu1)
        set -- $header_octets
        [ "$#" -eq 4 ] && [ "$1" -eq 0 ] && [ "$2" -eq 0 ] || return 1
        header_size=$(($3 * 256 + $4))
        [ "$header_size" -ge 100 ] && [ "$header_size" -le 512 ]
        ;;
      pamtowinicon)   [ "$(hexprefix "$encoded" 4)" = 00000100 ] ;;
      pamtohdiff)     [ "$(hexprefix "$encoded" 2)" = 5037 ] ;;
      pnmtosir)       [ "$(hexprefix "$encoded" 2)" = 4f3a ] ;;
      ppmtoyuv)       [ "$(busy wc -c < "$encoded")" -eq 128 ] ;;
      *) return 2 ;;
    esac
}
image_check() {
    decoded=$1
    dimensions=$2
    image_type=$3
    inspect=$4
    [ -s "$decoded" ] || return 1
    case "$(hexprefix "$decoded" 2)" in
      5031|5032|5033|5034|5035|5036|5037) ;;
      *) return 1 ;;
    esac
    /usr/bin/pamfile "$decoded" > "$inspect" 2>&1 || return 1
    busy grep -Fq "$dimensions" "$inspect" || return 1
    busy grep -Fq "$image_type" "$inspect" || return 1
    # The decoded file contains actual sample values, not just an image
    # header. Limit legacy fixed-canvas formats to 8x8 to keep logs bounded.
    /usr/bin/pamcut -left 0 -top 0 -width 8 -height 8 "$decoded" \
        > "$inspect-crop" 2> "$inspect-crop-error" || return 1
    /usr/bin/pamtable "$inspect-crop" > "$inspect-pixels" 2>&1 || return 1
    [ "$(busy wc -l < "$inspect-pixels")" -eq 8 ] || return 1
    busy grep -qE '[1-9]' "$inspect-pixels"
}
case_run() {
    app=$1
    work="/tmp/m158-$app"
    busy mkdir -p "$work"
    exe=$(original "$app") || return 1
    src=/tests/m158-color.ppm
    dimensions='8 by 8'
    image_type=PPM
    case "$app" in
      ppmtopcx|pcxtoppm) encoder=ppmtopcx; decoder=pcxtoppm ;;
      ppmtoilbm|ilbmtoppm) encoder=ppmtoilbm; decoder=ilbmtoppm ;;
      ppmtoleaf|leaftoppm) encoder=ppmtoleaf; decoder=leaftoppm ;;
      ppmtoneo|neotoppm)
        encoder=ppmtoneo; decoder=neotoppm
        src=/tests/m158-palette.ppm; dimensions='320 by 200' ;;
      ppmtopj|pjtoppm) encoder=ppmtopj; decoder=pjtoppm ;;
      ppmtopi1|pi1toppm)
        encoder=ppmtopi1; decoder=pi1toppm
        src=/tests/m158-palette.ppm; dimensions='320 by 200' ;;
      pgmtofs|fstopgm)
        encoder=pgmtofs; decoder=fstopgm
        src=/tests/m158-gray.pgm; image_type=PGM ;;
      pgmtolispm|lispmtopgm)
        encoder=pgmtolispm; decoder=lispmtopgm
        src=/tests/m158-gray.pgm; image_type=PGM ;;
      pbmtoicon|icontopbm)
        encoder=pbmtoicon; decoder=icontopbm
        src=/tests/m158-bw.pbm
        dimensions='16 by 8'; image_type=PBM ;;
      pbmtomacp|macptopbm)
        encoder=pbmtomacp; decoder=macptopbm
        src=/tests/m158-bw.pbm
        dimensions='576 by 720'; image_type=PBM ;;
      pnmtoxwd|xwdtopnm) encoder=pnmtoxwd; decoder=xwdtopnm ;;
      pamtowinicon|winicontopam)
        encoder=pamtowinicon; decoder=winicontopam; image_type=PAM ;;
      pamtohdiff|hdifftopam)
        encoder=pamtohdiff; decoder=hdifftopam; image_type=PAM ;;
      pnmtosir|sirtopnm) encoder=pnmtosir; decoder=sirtopnm ;;
      ppmtoyuv|yuvtoppm) encoder=ppmtoyuv; decoder=yuvtoppm ;;
      *) echo "[M158 UNKNOWN ELF CASE] $app" >&2; return 2 ;;
    esac
    enc=$(original "$encoder") || return 1
    dec=$(original "$decoder") || return 1
    [ "$app" = "$encoder" ] && enc="$exe"
    [ "$app" = "$decoder" ] && dec="$exe"
    encoded="$work/encoded"
    decoded="$work/decoded.pnm"
    "$enc" "$src" > "$encoded" 2> "$work/encode-error" || return 1
    format_check "$encoder" "$encoded" || return 1
    if [ "$decoder" = yuvtoppm ]; then
        "$dec" 8 8 "$encoded" > "$decoded" 2> "$work/decode-error" || return 1
    else
        "$dec" "$encoded" > "$decoded" 2> "$work/decode-error" || return 1
    fi
    image_check "$decoded" "$dimensions" "$image_type" "$work/inspect"
}
if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in ppmtopcx pcxtoppm ppmtoilbm ilbmtoppm ppmtoleaf leaftoppm \
           ppmtoneo neotoppm ppmtopj pjtoppm ppmtopi1 pi1toppm \
           pgmtofs fstopgm pgmtolispm lispmtopgm pbmtoicon icontopbm \
           pbmtomacp macptopbm pnmtoxwd xwdtopnm pamtowinicon winicontopam \
           pamtohdiff hdifftopam pnmtosir sirtopnm ppmtoyuv yuvtoppm; do
    echo "[M158 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m158-$app-case.log"
    if /bin/sh /tests/m158-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M158_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M158 ORIGINAL IMAGE FUNCTION FAIL] $app rc=$rc" >&2
        busy tail -30 "$detail" >&2 || :
        for stderr in /tmp/m158-$app/encode-error /tmp/m158-$app/decode-error; do
            [ -s "$stderr" ] && busy head -c 480 "$stderr" >&2 || :
        done
        printf 'M158_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M158 BATCH FAIL] $failures/30 genuine image codec operations" >&2
    echo M158_BATCH_FAIL
    exit 1
fi
echo M158_BATCH_PASS
