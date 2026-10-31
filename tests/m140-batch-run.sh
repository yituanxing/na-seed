#!/bin/sh
# M140: independent functional acceptance of 20 ORIGINAL signed GNU ELF
# executables in NaSeed's native system-QEMU guest (not BusyBox applets).
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
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
seed_lines() {
    busy printf '%s\n' cherry apple banana apple > "$work/lines"
}
seed_image() {
    busy printf '%s\n' 'P3' '4 4' '255' \
        '255 0 0  0 255 0  0 0 255  255 255 255' \
        '0 0 0  128 64 32  32 64 128  255 0 255' \
        '255 255 0  10 20 30  30 20 10  0 255 255' \
        '1 2 3  16 32 48  96 80 64  220 210 200' > "$work/source.ppm"
}
seed_gray() {
    busy printf '%s\n' 'P2' '5 5' '255' \
        '0 20 40 60 80' '10 30 50 70 90' '100 120 140 160 180' \
        '5 15 25 35 45' '255 240 200 120 60' > "$work/source.pgm"
}
seed_bitmap() {
    busy printf '%s\n' 'P1' '8 8' \
        '0 0 0 0 0 0 0 0' '0 1 1 0 0 1 1 0' \
        '0 1 0 1 1 0 1 0' '0 0 1 0 0 1 0 0' \
        '0 0 1 0 0 1 0 0' '0 1 0 1 1 0 1 0' \
        '0 1 1 0 0 1 1 0' '0 0 0 0 0 0 0 0' > "$work/source.pbm"
}
verify_image() {
    [ -s "$work/result.pnm" ]
    /usr/bin/pamfile "$work/result.pnm" > "$work/geometry" 2>&1
    busy grep -Fq "$1 by $2" "$work/geometry"
}
run_image_case() {
    case "$app" in
      pbmclean|pbmreduce|pbmnoise) seed_bitmap ;;
      pgmedge|pgmmedian|pgmhist) seed_gray ;;
      *) seed_image ;;
    esac
    case "$app" in
      pbmclean) "$exe" "$work/source.pbm" > "$work/result.pnm" 2> "$work/output"
                verify_image 8 8 ;;
      pbmreduce) "$exe" 2 "$work/source.pbm" > "$work/result.pnm" 2> "$work/output"
                 verify_image 4 4 ;;
      pgmedge|pgmmedian)
        "$exe" "$work/source.pgm" > "$work/result.pnm" 2> "$work/output"
        verify_image 5 5 ;;
      pgmhist)
        "$exe" "$work/source.pgm" > "$work/output" 2> "$work/error"
        [ -s "$work/output" ] ;;
      pamgauss)
        "$exe" 4 4 '-sigma=1' > "$work/result.pnm" 2> "$work/output"
        verify_image 4 4
        busy grep -q '^P7' "$work/result.pnm" ;;
      pnmnorm|pnmsmooth)
        "$exe" "$work/source.ppm" > "$work/result.pnm" 2> "$work/output"
        verify_image 4 4 ;;
      pnmhistmap)
        "$exe" "$work/source.ppm" > "$work/result.pnm" 2> "$work/output"
        [ -s "$work/result.pnm" ]
        /usr/bin/pamfile "$work/result.pnm" > "$work/geometry" ;;
      pnmshear)
        "$exe" 0 "$work/source.ppm" > "$work/result.pnm" 2> "$work/output"
        verify_image 4 4 ;;
      pnmmontage)
        "$exe" "$work/source.ppm" "$work/source.ppm" \
            > "$work/result.pnm" 2> "$work/output"
        [ -s "$work/result.pnm" ]
        /usr/bin/pamfile "$work/result.pnm" > "$work/geometry" ;;
      pnmtofits)
        "$exe" "$work/source.ppm" > "$work/result.fits" 2> "$work/output"
        [ -s "$work/result.fits" ]
        [ "$(busy head -c 6 "$work/result.fits")" = SIMPLE ] ;;
      ppmtoascii)
        "$exe" "$work/source.ppm" > "$work/output" 2> "$work/error"
        [ -s "$work/output" ] ;;
      pamtotiff)
        "$exe" "$work/source.ppm" > "$work/result.tiff" 2> "$work/output"
        [ -s "$work/result.tiff" ]
        magic=$(busy dd if="$work/result.tiff" bs=1 count=2 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        case "$magic" in 4949|4d4d) ;; *) return 1 ;; esac ;;
      ppmcolors)
        "$exe" "$work/source.ppm" > "$work/result.pnm" 2> "$work/output"
        [ -s "$work/result.pnm" ]
        /usr/bin/pamfile "$work/result.pnm" > "$work/geometry" ;;
      pnmpaste)
        busy printf '%s\n' 'P3' '1 1' '255' '0 0 0' > "$work/overlay.ppm"
        "$exe" "$work/overlay.ppm" 1 1 "$work/source.ppm" \
            > "$work/result.pnm" 2> "$work/output"
        verify_image 4 4 ;;
      pbmnoise)
        "$exe" 8 8 '-ratio=1/2' '-randomseed=42' \
            > "$work/result.pnm" 2> "$work/output"
        verify_image 8 8 ;;
      pamditherbw|ppmdither)
        "$exe" "$work/source.ppm" > "$work/result.pnm" 2> "$work/output"
        verify_image 4 4 ;;
      *) echo "[M140 UNKNOWN NETPBM ELF] $app" >&2; return 2 ;;
    esac
}
run_case() {
    app=$1
    work="/tmp/m140-$app"
    busy mkdir -p "$work"
    exe=$(original_elf "/usr/bin/$app")
    case "$app" in
      sort)
        seed_lines
        "$exe" "$work/lines" > "$work/output"
        busy printf '%s\n' apple apple banana cherry > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      uniq)
        busy printf '%s\n' red red blue blue blue green > "$work/lines"
        "$exe" -c "$work/lines" > "$work/output"
        busy grep -Eq '^[[:space:]]*2[[:space:]]+red$' "$work/output"
        busy grep -Eq '^[[:space:]]*3[[:space:]]+blue$' "$work/output"
        busy grep -Eq '^[[:space:]]*1[[:space:]]+green$' "$work/output" ;;
      join)
        busy printf '%s\n' 'alpha one' 'beta two' > "$work/left"
        busy printf '%s\n' 'alpha A' 'beta B' > "$work/right"
        "$exe" "$work/left" "$work/right" > "$work/output"
        busy printf '%s\n' 'alpha one A' 'beta two B' > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      paste)
        busy printf '%s\n' a b > "$work/left"
        busy printf '%s\n' 1 2 > "$work/right"
        "$exe" "$work/left" "$work/right" > "$work/output"
        busy printf 'a\t1\nb\t2\n' > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      tr)
        busy printf '%s\n' 'abc abc' > "$work/lines"
        "$exe" 'a-c' 'A-C' < "$work/lines" > "$work/output"
        busy grep -Fxq 'ABC ABC' "$work/output" ;;
      wc)
        busy printf 'one two\nthree\n' > "$work/lines"
        "$exe" -l -w -c "$work/lines" > "$work/output"
        busy grep -Eq '^[[:space:]]*2[[:space:]]+3[[:space:]]+14[[:space:]]' "$work/output" ;;
      od)
        busy printf 'ABC' > "$work/data"
        "$exe" -An -tx1 "$work/data" > "$work/output"
        busy grep -Eq '41[[:space:]]+42[[:space:]]+43' "$work/output" ;;
      nl)
        busy printf '%s\n' red blue > "$work/lines"
        "$exe" -ba "$work/lines" > "$work/output"
        busy grep -Eq '^[[:space:]]*1[[:space:]]+red$' "$work/output"
        busy grep -Eq '^[[:space:]]*2[[:space:]]+blue$' "$work/output" ;;
      numfmt)
        "$exe" --to=iec 1048576 > "$work/output"
        busy grep -Eq '^1(\.0)?M$' "$work/output" ;;
      shuf)
        "$exe" -i 1-4 -n 4 > "$work/output"
        [ "$(busy wc -l < "$work/output")" -eq 4 ]
        /usr/bin/sort -n "$work/output" > "$work/ordered"
        busy printf '%s\n' 1 2 3 4 > "$work/expected"
        busy cmp -s "$work/ordered" "$work/expected" ;;
      split)
        busy printf '%s\n' a b c d e > "$work/lines"
        "$exe" -l 2 "$work/lines" "$work/chunk-"
        [ -s "$work/chunk-aa" ] && [ -s "$work/chunk-ab" ] &&
            [ -s "$work/chunk-ac" ]
        busy cat "$work/chunk-aa" "$work/chunk-ab" "$work/chunk-ac" \
            > "$work/output"
        busy cmp -s "$work/lines" "$work/output" ;;
      tsort)
        busy printf '%s\n' 'a b' 'b c' > "$work/edges"
        "$exe" "$work/edges" > "$work/output"
        busy printf '%s\n' a b c > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      tac)
        busy printf '%s\n' a b c > "$work/lines"
        "$exe" "$work/lines" > "$work/output"
        busy printf '%s\n' c b a > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      seq)
        "$exe" 3 7 > "$work/output"
        busy printf '%s\n' 3 4 5 6 7 > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      sha256sum)
        busy printf abc > "$work/data"
        "$exe" "$work/data" > "$work/output"
        busy grep -Fq 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' "$work/output" ;;
      md5sum)
        busy printf abc > "$work/data"
        "$exe" "$work/data" > "$work/output"
        busy grep -Fq '900150983cd24fb0d6963f7d28e17f72' "$work/output" ;;
      realpath)
        "$exe" -m "$work/a/../result" > "$work/output"
        busy grep -Fxq "$work/result" "$work/output" ;;
      readlink)
        busy printf '%s\n' payload > "$work/seed.bin"
        busy ln -s seed.bin "$work/link"
        "$exe" -f "$work/link" > "$work/output"
        busy grep -Fxq "$work/seed.bin" "$work/output" ;;
      sha1sum)
        busy printf abc > "$work/data"
        "$exe" "$work/data" > "$work/output"
        busy grep -Fq 'a9993e364706816aba3e25717850c26c9cd0d89d' "$work/output" ;;
      tail)
        busy printf '%s\n' a b c d > "$work/lines"
        "$exe" -n 2 "$work/lines" > "$work/output"
        busy printf '%s\n' c d > "$work/expected"
        busy cmp -s "$work/output" "$work/expected" ;;
      *) run_image_case ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M140_TRACE:-0}" = 1 ]; then
        PS4='[M140 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in sort uniq join paste tr wc od nl numfmt shuf split tsort \
           tac seq sha256sum md5sum realpath readlink sha1sum tail \
           pbmclean pbmreduce pgmedge pgmmedian pgmhist pamgauss \
           pnmnorm pnmsmooth pnmhistmap pnmshear pnmmontage pnmtofits \
           ppmtoascii pamtotiff ppmcolors pnmpaste pbmnoise pamditherbw ppmdither; do
    echo "[M140 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    log="/tmp/m140-$app-case.log"
    if M140_TRACE=1 /bin/sh /tests/m140-batch-run.sh --case "$app" \
            > "$log" 2>&1; then
        printf 'M140_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M140 $upper FAILED] signed ELF rc=$rc" >&2
        busy tail -110 "$log" >&2 || :
        for detail in output error expected lines data left right ordered \
                      edges geometry source.ppm source.pgm source.pbm \
                      result.pnm result.fits result.tiff; do
            if [ -s "/tmp/m140-$app/$detail" ]; then
                echo "[M140 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 850 "/tmp/m140-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M140_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M140 BATCH FAIL] $failed/39 functional GNU/Netpbm cases" >&2
    echo M140_BATCH_FAIL
    exit 1
fi
echo M140_BATCH_PASS
