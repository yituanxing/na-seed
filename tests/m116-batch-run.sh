#!/bin/sh
# M116: real, independently packaged Alpine Linux ELF media/codecs + data apps.
# The Linux host installs the signed APKs. NaSeed only executes their binaries.
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
    echo "[M116 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

make_png() {
    # Proven real RGB 16x16 PNG fixture from the M112 native suite.
    printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAABxklEQVR42g2SURHAMAhDsYAFLNQCFrCAhVnAAhawUAtYwAIWuuxjt2sZSR4QETGxkBw6SmpkTv7RFxRJWVSXblMPzdI+eijHQ8LnsCibsTp/H3twJkfxvVzNM9zL7/ESibAcETrCKm7yuegnFlIptySuZMuOvJV+MkRojXq8Dun57Lgf+47GuXmqTt4Tfd6c3TPvNJEqq4n60U+VTNlVPj2hnTqle/W1xmiu1tNLBNumgqbmik8jNwhJ2KR12bu2bTkWa/dZEbmzf+J63NTFHJ7ocw7f9Ffe16e9xu96PE8iIEFOmIcjyOBfHHwU38tv65v7dX93vtovH2hTBEdK1Imr0RbjsV+8CMrgCrlxOnTCNvzFRwTcGQIYWQqr2Y5GuYHjpErAkk4E0k2IOlEV15WKU6m1Vs+rv5ooyQJXusVdPgV/+sqIMErMB9BvKDAgGwzfDkiiDy4v9UUU3wtkStTNPdJ7+mmHdXrX1zdas63abyM1TfO2vD5EWJNpwRBnFegnHGCmAnFGC2LjjZKhHQxciHZ5n2yfHd2yhXZ8m7GeC5Z613plFrtBb5kIK4i9wrJgAzBWzAoDAFWgQn6EglPIoycKH/10EH6QXRZ/nQAAAABJRU5ErkJggg==' |
        busy base64 -d > "$1"
    [ -s "$1" ]
}

png_signature() {
    signature=$(busy dd if="$1" bs=1 count=8 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
    [ "$signature" = 89504e470d0a1a0a ]
}

make_ppm() {
    # 16x16 binary PPM, real RGB primary colors + yellow; 768 exact pixels.
    printf 'P6\n16 16\n255\n' > "$1"
    n=0
    while [ "$n" -lt 64 ]; do
        printf '\377\000\000\000\377\000\000\000\377\377\377\000' >> "$1"
        n=$((n + 1))
    done
    [ "$(busy wc -c < "$1")" -eq 781 ]
}

pixel_bytes() {
    # OpenJPEG honors PNG gAMA/sRGB metadata when reading pixels. avifdec
    # adds those ancillary chunks even when its decoded RGB is byte-exact,
    # so normalize metadata with the original signed pngcrush first. This
    # yields raw RGB comparisons without changing a single pixel: reproduced
    # and byte-verified against the CI Alpine rootfs on the Linux host.
    pixel_normalizer=$(original_elf /usr/bin/pngcrush)
    pixel_comp=$(original_elf /usr/bin/opj_compress)
    pixel_decomp=$(original_elf /usr/bin/opj_decompress)
    "$pixel_normalizer" -q -rem alla "$1" "$2.neutral.png" > "$2.normalize-log" 2>&1
    "$pixel_comp" -i "$2.neutral.png" -o "$2.jp2" -n 3 > "$2.compress-log" 2>&1
    "$pixel_decomp" -i "$2.jp2" -o "$2.ppm" > "$2.decode-log" 2>&1
    busy tail -c 768 "$2.ppm" > "$2.rgb"
    [ "$(busy wc -c < "$2.rgb")" -eq 768 ]
}

png_pixels_equal() {
    pixel_bytes "$1" "$3/source"
    pixel_bytes "$2" "$3/transformed"
    busy cmp "$3/source.rgb" "$3/transformed.rgb"
}

jp2_from_ppm() {
    jp2_encoder=$(original_elf /usr/bin/opj_compress)
    make_ppm "$1/source.ppm"
    "$jp2_encoder" -i "$1/source.ppm" -o "$1/image.jp2" -n 3 > "$1/encode-log"
    [ -s "$1/image.jp2" ]
}

check_lossless_jp2() {
    jp2_decoder=$(original_elf /usr/bin/opj_decompress)
    "$jp2_decoder" -i "$1/image.jp2" -o "$1/restored.ppm" > "$1/decode-log"
    busy tail -c 768 "$1/source.ppm" > "$1/original.rgb"
    busy tail -c 768 "$1/restored.ppm" > "$1/decoded.rgb"
    [ "$(busy wc -c < "$1/decoded.rgb")" -eq 768 ]
    busy cmp "$1/original.rgb" "$1/decoded.rgb"
}

make_jpeg() {
    jpeg_encoder=$(original_elf /usr/bin/cjpeg)
    make_ppm "$1/source.ppm"
    # Disable chroma subsampling to preserve the red primary pixel strongly.
    "$jpeg_encoder" -quality 100 -sample 1x1 -outfile "$1/image.jpg" "$1/source.ppm"
    [ -s "$1/image.jpg" ]
}

jpeg_signature() {
    signature=$(busy dd if="$1" bs=1 count=2 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
    [ "$signature" = ffd8 ]
}

check_red_pixel() {
    # The source PPM's very first RGB pixel is (255,0,0). JPEG is lossy,
    # so test strong true-color bounds instead of a fraudulent byte-for-byte
    # comparison. These values come from the original djpeg-decoded image.
    set -- $(busy od -An -tu1 -N3 "$1")
    [ "$#" -eq 3 ] || return 1
    [ "$1" -gt 200 ] && [ "$2" -lt 60 ] && [ "$3" -lt 60 ]
}

decode_jpeg() {
    jpeg_decoder=$(original_elf /usr/bin/djpeg)
    "$jpeg_decoder" -pnm -outfile "$2" "$1"
    [ "$(busy wc -c < "$2")" -ge 781 ]
    busy tail -c 768 "$2" > "$2.rgb"
    [ "$(busy wc -c < "$2.rgb")" -eq 768 ]
    check_red_pixel "$2.rgb"
}

make_avif() {
    avif_encoder=$(original_elf /usr/bin/avifenc)
    make_png "$1/source.png"
    # Lossless AV1 quantization alone does not make RGB<->YUV conversion
    # reversible. Identity matrix coefficients (CICP 1/13/0), full range,
    # and 4:4:4 preserve the original RGB channels byte-for-byte; continue
    # verifying actual decoded pixels, not just a valid AVIF container.
    "$avif_encoder" --lossless --cicp 1/13/0 --range full --yuv 444 \
        -s 10 -j 1 "$1/source.png" "$1/image.avif" > "$1/encode-log"
    [ -s "$1/image.avif" ]
    avif_brand=$(busy dd if="$1/image.avif" bs=1 skip=4 count=8 2>/dev/null)
    [ "$avif_brand" = ftypavif ]
}

check_decoded_avif() {
    avif_decoder=$(original_elf /usr/bin/avifdec)
    "$avif_decoder" -j 1 "$1/image.avif" "$1/decoded.png" > "$1/decode-log"
    png_signature "$1/decoded.png"
    png_pixels_equal "$1/source.png" "$1/decoded.png" "$1"
}

run_case() {
    app=$1
    work="/tmp/m116-$app"
    busy mkdir -p "$work"
    case "$app" in
      pngcrush)
        exe=$(original_elf /usr/bin/pngcrush)
        make_png "$work/source.png"
        "$exe" -q "$work/source.png" "$work/optimized.png"
        png_signature "$work/optimized.png"
        png_pixels_equal "$work/source.png" "$work/optimized.png" "$work" ;;
      oxipng)
        exe=$(original_elf /usr/bin/oxipng)
        make_png "$work/source.png"
        busy cp "$work/source.png" "$work/optimized.png"
        "$exe" -o 2 --force --threads 1 "$work/optimized.png" > "$work/optimize-log"
        png_signature "$work/optimized.png"
        png_pixels_equal "$work/source.png" "$work/optimized.png" "$work" ;;
      opj_compress)
        exe=$(original_elf /usr/bin/opj_compress)
        make_ppm "$work/source.ppm"
        "$exe" -i "$work/source.ppm" -o "$work/image.jp2" -n 3 > "$work/encode-log"
        [ -s "$work/image.jp2" ]
        check_lossless_jp2 "$work" ;;
      opj_decompress)
        exe=$(original_elf /usr/bin/opj_decompress)
        jp2_from_ppm "$work"
        "$exe" -i "$work/image.jp2" -o "$work/restored.ppm" > "$work/decode-log"
        busy tail -c 768 "$work/source.ppm" > "$work/source.rgb"
        busy tail -c 768 "$work/restored.ppm" > "$work/restored.rgb"
        busy cmp "$work/source.rgb" "$work/restored.rgb" ;;
      opj_dump)
        exe=$(original_elf /usr/bin/opj_dump)
        jp2_from_ppm "$work"
        "$exe" -i "$work/image.jp2" > "$work/metadata"
        [ -s "$work/metadata" ]
        busy grep -Eq 'numcomps[[:space:]]*=[[:space:]]*3' "$work/metadata"
        busy grep -Eq 'x1[[:space:]]*=[[:space:]]*16' "$work/metadata" ;;
      avifenc)
        exe=$(original_elf /usr/bin/avifenc)
        make_avif "$work"
        check_decoded_avif "$work" ;;
      avifdec)
        exe=$(original_elf /usr/bin/avifdec)
        make_avif "$work"
        "$exe" -j 1 "$work/image.avif" "$work/decoded.png" > "$work/decode-log"
        png_signature "$work/decoded.png"
        png_pixels_equal "$work/source.png" "$work/decoded.png" "$work" ;;
      cjpeg)
        exe=$(original_elf /usr/bin/cjpeg)
        make_jpeg "$work"
        jpeg_signature "$work/image.jpg"
        decode_jpeg "$work/image.jpg" "$work/restored.ppm" ;;
      djpeg)
        exe=$(original_elf /usr/bin/djpeg)
        make_jpeg "$work"
        "$exe" -pnm -outfile "$work/restored.ppm" "$work/image.jpg"
        [ "$(busy wc -c < "$work/restored.ppm")" -ge 781 ]
        busy tail -c 768 "$work/restored.ppm" > "$work/pixels.rgb"
        [ -s "$work/pixels.rgb" ]
        check_red_pixel "$work/pixels.rgb" ;;
      jpegtran)
        exe=$(original_elf /usr/bin/jpegtran)
        make_jpeg "$work"
        "$exe" -copy none -optimize -outfile "$work/optimized.jpg" "$work/image.jpg"
        jpeg_signature "$work/optimized.jpg"
        decode_jpeg "$work/image.jpg" "$work/original.ppm"
        decode_jpeg "$work/optimized.jpg" "$work/optimized.ppm"
        busy cmp "$work/original.ppm.rgb" "$work/optimized.ppm.rgb" ;;
      jq)
        exe=$(original_elf /usr/bin/jq)
        printf '{"lines":[{"n":2},{"n":5},{"n":8}],"label":"NaSeed"}\n' > "$work/input.json"
        "$exe" -r '[.lines[].n * 3] | add' "$work/input.json" > "$work/result"
        printf '45\n' > "$work/expected"
        busy cmp "$work/expected" "$work/result" ;;
      sqlite3)
        exe=$(original_elf /usr/bin/sqlite3)
        "$exe" "$work/items.db" 'CREATE TABLE items(id INTEGER PRIMARY KEY, n INTEGER); INSERT INTO items(n) VALUES (8),(13),(21);' > "$work/initial"
        [ -s "$work/items.db" ]
        "$exe" "$work/items.db" 'BEGIN; UPDATE items SET n=n+1 WHERE id=2; COMMIT; SELECT SUM(n) FROM items;' > "$work/output"
        printf '43\n' > "$work/expected"
        busy cmp "$work/expected" "$work/output"
        "$exe" "$work/items.db" 'SELECT COUNT(*) FROM items;' > "$work/count"
        busy grep -Fxq '3' "$work/count" ;;
      zip)
        exe=$(original_elf /usr/bin/zip)
        printf 'M116 original ZIP preserves exact bytes: a\n' > "$work/a"
        printf 'M116 original ZIP preserves exact bytes: b\n' > "$work/b"
        (cd "$work"; "$exe" -q archive.zip a b)
        [ -s "$work/archive.zip" ]
        zip_reader=$(original_elf /usr/bin/unzip)
        "$zip_reader" -p "$work/archive.zip" a > "$work/extracted-a"
        "$zip_reader" -p "$work/archive.zip" b > "$work/extracted-b"
        busy cmp "$work/a" "$work/extracted-a"
        busy cmp "$work/b" "$work/extracted-b" ;;
      unzip)
        exe=$(original_elf /usr/bin/unzip)
        printf 'M116 unzip distinct source contents\n' > "$work/content"
        zip_writer=$(original_elf /usr/bin/zip)
        (cd "$work"; "$zip_writer" -q archive.zip content)
        busy mkdir -p "$work/extracted"
        "$exe" -q "$work/archive.zip" -d "$work/extracted"
        busy cmp "$work/content" "$work/extracted/content"
        "$exe" -t "$work/archive.zip" > "$work/integrity"
        busy grep -Eiq 'no errors|OK' "$work/integrity" ;;
      zstd)
        exe=$(original_elf /usr/bin/zstd)
        printf 'NaSeed M116 signed original Zstandard file contents\n' > "$work/source"
        "$exe" -q -3 -f -o "$work/data.zst" "$work/source"
        [ -s "$work/data.zst" ]
        "$exe" -q -d -f -o "$work/restored" "$work/data.zst"
        busy cmp "$work/source" "$work/restored" ;;
      *) echo "[M116 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    if [ "${M116_TRACE:-0}" = 1 ]; then
        PS4='[M116 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m116-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in pngcrush oxipng opj_compress opj_decompress opj_dump \
           avifenc avifdec cjpeg djpeg jpegtran jq sqlite3 zip unzip zstd; do
    echo "[M116 CASE START] $app"
    case_log="/tmp/m116-$app-case.log"
    if M116_TRACE=1 /bin/sh /tests/m116-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M116_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M116 $upper FAILED] signed original Linux ELF functional oracle, rc=$rc" >&2
        busy tail -100 "$case_log" >&2
        printf 'M116_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M116_BATCH_PASS
else
    echo "[M116 BATCH FAIL] $failed/15 original Linux programs" >&2
    echo M116_BATCH_FAIL
    exit 1
fi
