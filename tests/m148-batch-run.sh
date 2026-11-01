#!/bin/sh
# M148: real signed Alpine image codec interoperability, NEVER --help probes.
# 13 encoder+decoder roundtrips (26 new ELFs) and nine new decoders paired
# with already accepted real native encoders from M146 and earlier batches.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }

original_elf() {
    elf=$1
    [ -x "$elf" ] || return 1
    resolved=$(busy readlink -f "$elf") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$elf"
}
seed_images() {
    # 16x16 PBM supplies nontrivial white and black fields and fits legacy
    # formats that use 16-bit raster storage and byte-aligned row boundaries.
    busy printf '%s\n' 'P1' '16 16' \
        '0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0' \
        '0 1 1 0 0 0 0 1 1 0 0 0 0 1 1 0' \
        '0 1 0 1 0 0 1 0 0 1 0 0 1 0 1 0' \
        '0 0 1 0 1 1 0 0 0 0 1 1 0 1 0 0' \
        '0 0 1 0 1 1 0 0 0 0 1 1 0 1 0 0' \
        '0 1 0 1 0 0 1 0 0 1 0 0 1 0 1 0' \
        '0 1 1 0 0 0 0 1 1 0 0 0 0 1 1 0' \
        '0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0' \
        '0 0 1 1 1 1 0 0 0 0 1 1 1 1 0 0' \
        '0 1 0 0 0 0 1 0 0 1 0 0 0 0 1 0' \
        '0 1 0 0 0 0 1 0 0 1 0 0 0 0 1 0' \
        '0 1 0 0 0 0 1 0 0 1 0 0 0 0 1 0' \
        '0 0 1 1 1 1 0 0 0 0 1 1 1 1 0 0' \
        '0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0' \
        '0 1 0 1 0 1 0 1 1 0 1 0 1 0 1 0' \
        '0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0' \
        > "$work/original.pbm"
    busy printf '%s\n' 'P3' '4 4' '255' \
        '255 0 0  0 255 0  0 0 255  255 255 255' \
        '0 0 0  128 64 32  32 64 128  255 0 255' \
        '255 255 0  10 20 30  30 20 10  0 255 255' \
        '1 2 3  16 32 48  96 80 64  220 210 200' \
        > "$work/original.ppm"
}
verify_decoded() {
    [ -s "$work/encoded" ] && [ -s "$work/decoded.pnm" ] || return 1
    /usr/bin/pamfile "$work/decoded.pnm" > "$work/geometry" 2>&1 || return 1
    busy grep -Fq "$1 by $2" "$work/geometry" || return 1
    # The output must parse as a true Netpbm image, not a text/help message.
    busy grep -Eq '([Pp][Bb][Mm]|[Pp][Gg][Mm]|[Pp][Pp][Mm]|[Pp][Aa][Mm])' "$work/geometry"
}
run_codec_pair() {
    encode_name=$1 decode_name=$2 source_kind=$3
    encoder=$(original_elf "/usr/bin/$encode_name")
    decoder=$(original_elf "/usr/bin/$decode_name")
    if [ "$source_kind" = pbm ]; then
        source="$work/original.pbm"
        width=16 height=16
    else
        source="$work/original.ppm"
        width=4 height=4
    fi
    "$encoder" "$source" > "$work/encoded" 2> "$work/encode-error"
    case "$decode_name" in
      g3topbm) "$decoder" -width=16 "$work/encoded" \
                   > "$work/decoded.pnm" 2> "$work/decode-error" ;;
      *) "$decoder" "$work/encoded" \
             > "$work/decoded.pnm" 2> "$work/decode-error" ;;
    esac
    verify_decoded "$width" "$height"
}
run_single_decoder() {
    encoder_name=$1 decoder_name=$2 source_kind=$3
    encoder=$(original_elf "/usr/bin/$encoder_name")
    decoder=$(original_elf "/usr/bin/$decoder_name")
    if [ "$source_kind" = pbm ]; then
        source="$work/original.pbm"
        width=16 height=16
    else
        source="$work/original.ppm"
        width=4 height=4
    fi
    case "$encoder_name" in
      pnmtopalm) "$encoder" -depth=16 "$source" \
                      > "$work/encoded" 2> "$work/encode-error" ;;
      *) "$encoder" "$source" > "$work/encoded" 2> "$work/encode-error" ;;
    esac
    "$decoder" "$work/encoded" > "$work/decoded.pnm" 2> "$work/decode-error"
    verify_decoded "$width" "$height"
}
run_case() {
    case_id=$1
    work="/tmp/m148-$case_id"
    busy mkdir -p "$work"
    seed_images
    case "$case_id" in
      atk) run_codec_pair pbmtoatk atktopbm pbm ;;
      cmuwm) run_codec_pair pbmtocmuwm cmuwmtopbm pbm ;;
      gem) run_codec_pair pbmtogem gemtopnm pbm ;;
      mda) run_codec_pair pbmtomda mdatopbm pbm ;;
      mgr) run_codec_pair pbmtomgr mgrtopbm pbm ;;
      mrf) run_codec_pair pbmtomrf mrftopbm pbm ;;
      ybm) run_codec_pair pbmtoybm ybmtopbm pbm ;;
      g3) run_codec_pair pbmtog3 g3topbm pbm ;;
      pfm) run_codec_pair pamtopfm pfmtopam ppm ;;
      avs) run_codec_pair pamtoavs avstopam ppm ;;
      srf) run_codec_pair pamtosrf srftopam ppm ;;
      pdbimg) run_codec_pair pamtopdbimg pdbimgtopam pbm ;;
      cis) run_codec_pair pbmtocis cistopbm pbm ;;
      sunicon) run_single_decoder pbmtosunicon sunicontopnm pbm ;;
      xbm) run_single_decoder pbmtoxbm xbmtopbm pbm ;;
      fits) run_single_decoder pamtofits fitstopnm ppm ;;
      sgi) run_single_decoder pnmtosgi sgitopnm ppm ;;
      rle) run_single_decoder pnmtorle rletopnm ppm ;;
      palm) run_single_decoder pnmtopalm palmtopnm ppm ;;
      xvmini) run_single_decoder pamtoxvmini xvminitoppm ppm ;;
      tiff) run_single_decoder pnmtotiffcmyk tifftopnm ppm ;;
      jpeg) run_single_decoder pnmtojpeg jpegtopnm ppm ;;
      *) echo "[M148 UNKNOWN CASE] $case_id" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for entry in \
    'atk:pbmtoatk:atktopbm' 'cmuwm:pbmtocmuwm:cmuwmtopbm' \
    'gem:pbmtogem:gemtopnm' 'mda:pbmtomda:mdatopbm' \
    'mgr:pbmtomgr:mgrtopbm' 'mrf:pbmtomrf:mrftopbm' \
    'ybm:pbmtoybm:ybmtopbm' 'g3:pbmtog3:g3topbm' \
    'pfm:pamtopfm:pfmtopam' 'avs:pamtoavs:avstopam' \
    'srf:pamtosrf:srftopam' 'pdbimg:pamtopdbimg:pdbimgtopam' \
    'cis:pbmtocis:cistopbm' \
    'sunicon:sunicontopnm' 'xbm:xbmtopbm' 'fits:fitstopnm' \
    'sgi:sgitopnm' 'rle:rletopnm' 'palm:palmtopnm' \
    'xvmini:xvminitoppm' 'tiff:tifftopnm' 'jpeg:jpegtopnm'; do
    case_id=${entry%%:*}
    rest=${entry#*:}
    marker_one=${rest%%:*}
    marker_two=''
    case "$rest" in *:*) marker_two=${rest#*:} ;; esac
    echo "[M148 CASE START] $case_id"
    detail="/tmp/m148-$case_id-case.log"
    if /bin/sh /tests/m148-batch-run.sh --case "$case_id" > "$detail" 2>&1; then
        upper=$(printf '%s' "$marker_one" | busy tr '[:lower:]' '[:upper:]')
        printf 'M148_%s_PASS\n' "$upper"
        if [ -n "$marker_two" ]; then
            upper=$(printf '%s' "$marker_two" | busy tr '[:lower:]' '[:upper:]')
            printf 'M148_%s_PASS\n' "$upper"
        fi
    else
        rc=$?
        echo "[M148 CASE FAILED] $case_id rc=$rc" >&2
        busy tail -100 "$detail" >&2 || :
        for artifact in encode-error decode-error geometry encoded decoded.pnm; do
            if [ -s "/tmp/m148-$case_id/$artifact" ]; then
                echo "[M148 DETAIL] $case_id/$artifact" >&2
                busy head -c 512 "/tmp/m148-$case_id/$artifact" >&2 || :
                echo >&2
            fi
        done
        printf 'M148_%s_FAIL\n' "$case_id"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M148 BATCH FAIL] $failed/22 original codec roundtrips failed" >&2
    echo M148_BATCH_FAIL
    exit 1
fi
echo M148_BATCH_PASS
