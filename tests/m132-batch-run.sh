#!/bin/sh
# M132: 15 globally new signed Alpine ELF files in one complete guest census.
# Execute real Unicode conversion/resource compilation, image codecs, lossless
# compression and PCRE2 capture semantics. Never count --help as a PASS.
# One failure never hides any other software case on the same system QEMU.
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

seed_icu() {
    busy printf 'm132 { greeting { "Hello 132 from Unicode" } }\n' > "$work/m132.txt"
    /usr/bin/genrb -s "$work" -d "$work" m132.txt
    [ -s "$work/m132.res" ]
}

package_icu() {
    seed_icu
    busy printf 'm132.res\n' > "$work/list.txt"
    (cd "$work" && /usr/sbin/gencmn -n m132 -d . 0 list.txt)
    [ -s "$work/m132.dat" ]
}

seed_image() {
    # libwebp's cwebp decodes binary P6, not the human-readable P3 Netpbm
    # fixture used by M131. This single shared fixture feeds four codecs.
    busy printf 'P6\n2 2\n255\n\377\000\000\000\377\000\000\000\377\377\377\000' > "$work/source.ppm"
    # The two established WebP utilities are fixture generators only;
    # neither is counted as a new M132 ELF (they appeared in M113).
    /usr/bin/cwebp -quiet -lossless "$work/source.ppm" -o "$work/source.webp"
    # Two independently constructed 2x2 RGB PNGs (PNG CRCs and zlib IDATs
    # generated on the host). Alpine dwebp's PNG encoder is not a dependency
    # of any of these tests; keep image oracles independent of it.
    busy printf 'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAFElEQVR4nGP4z8DAAMIM////ZwAAHu8E/KPItPcAAAAASUVORK5CYII=\n' |
        busy base64 -d > "$work/frame.png"
    busy printf 'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAEklEQVR4nGNgYPj//z+QYACRACrjBfv/D0OXAAAAAElFTkSuQmCC\n' |
        busy base64 -d > "$work/frame2.png"
    [ -s "$work/frame.png" ] && [ -s "$work/frame2.png" ] &&
        [ -s "$work/source.webp" ]
}

run_case() {
    app=$1
    work="/tmp/m132-$app"
    busy mkdir -p "$work"
    case "$app" in
      uconv)
        exe=$(original_elf /usr/bin/uconv)
        busy printf 'NaSeed-132: caf\303\251 \344\270\255\346\226\207\n' > "$work/unicode.txt"
        "$exe" -f UTF-8 -t UTF-16LE "$work/unicode.txt" > "$work/utf16"
        "$exe" -f UTF-16LE -t UTF-8 "$work/utf16" > "$work/decoded.txt"
        busy cmp -s "$work/unicode.txt" "$work/decoded.txt"
        ! busy cmp -s "$work/unicode.txt" "$work/utf16" ;;
      genrb)
        original_elf /usr/bin/genrb >/dev/null
        seed_icu
        [ -s "$work/m132.res" ]
        ! busy cmp -s "$work/m132.txt" "$work/m132.res" ;;
      derb)
        exe=$(original_elf /usr/bin/derb)
        seed_icu
        "$exe" -s "$work" -c m132.res > "$work/dumped.txt"
        busy grep -Fq 'Hello 132 from Unicode' "$work/dumped.txt" ;;
      genccode)
        exe=$(original_elf /usr/sbin/genccode)
        seed_icu
        "$exe" -d "$work" "$work/m132.res"
        [ -s "$work/m132_res.c" ]
        # ICU emits decimal literals for some byte values, so the actual
        # generated typed byte array is the stable semantic check.
        busy grep -Fq 'uint8_t bytes[' "$work/m132_res.c" ;;
      gencmn)
        original_elf /usr/sbin/gencmn >/dev/null
        package_icu
        ! busy cmp -s "$work/m132.res" "$work/m132.dat"
        busy grep -aq 'm132.res' "$work/m132.dat" ;;
      icupkg)
        exe=$(original_elf /usr/sbin/icupkg)
        seed_icu
        # icupkg also accepts standalone ICU resource data, not just .dat
        # archives. Test real endian transforms and verify restored resource
        # semantics; ICU can legitimately normalize unrelated header bytes.
        "$exe" -tb "$work/m132.res" "$work/bigendian.res"
        [ -s "$work/bigendian.res" ]
        ! busy cmp -s "$work/m132.res" "$work/bigendian.res"
        "$exe" -tl "$work/bigendian.res" "$work/restored.res"
        [ -s "$work/restored.res" ]
        /usr/bin/derb -s "$work" -c restored.res > "$work/restored.txt"
        busy grep -Fq 'Hello 132 from Unicode' "$work/restored.txt" ;;
      makeconv)
        exe=$(original_elf /usr/bin/makeconv)
        busy printf '<code_set_name> "m132"\n<mb_cur_min> 1\n<mb_cur_max> 1\n<uconv_class> "SBCS"\n<subchar> \\x3F\nCHARMAP\n<U003F> \\x3F |0\n<U0041> \\x41 |0\n<U0042> \\x42 |0\nEND CHARMAP\n' > "$work/m132.ucm"
        "$exe" -d "$work" "$work/m132.ucm"
        [ -s "$work/m132.cnv" ]
        ! busy cmp -s "$work/m132.ucm" "$work/m132.cnv" ;;
      gif2webp)
        exe=$(original_elf /usr/bin/gif2webp)
        # Standard one-pixel transparent GIF89a, with a genuine LZW frame.
        busy printf 'R0lGODlhAQABAAD/ACwAAAAAAQABAAACAUwAOw==\n' |
            busy base64 -d > "$work/single.gif"
        "$exe" "$work/single.gif" -o "$work/from-gif.webp"
        [ -s "$work/from-gif.webp" ]
        busy grep -aq 'WEBP' "$work/from-gif.webp" ;;
      img2webp)
        exe=$(original_elf /usr/bin/img2webp)
        seed_image
        "$exe" -loop 0 -d 100 "$work/frame.png" -d 120 "$work/frame2.png" \
            -o "$work/animation.webp"
        [ -s "$work/animation.webp" ]
        busy grep -aq 'ANIM' "$work/animation.webp"
        busy grep -aq 'ANMF' "$work/animation.webp" ;;
      webpinfo)
        exe=$(original_elf /usr/bin/webpinfo)
        seed_image
        "$exe" -summary "$work/source.webp" > "$work/info.txt"
        busy grep -Eq 'Width|width|2 x 2' "$work/info.txt"
        busy grep -Eq 'Height|height|2 x 2' "$work/info.txt" ;;
      webpmux)
        exe=$(original_elf /usr/bin/webpmux)
        seed_image
        busy printf '<x:xmpmeta xmlns:x="adobe:ns:meta/">NaSeed 132 XMP</x:xmpmeta>\n' > "$work/metadata.xmp"
        "$exe" -set xmp "$work/metadata.xmp" "$work/source.webp" -o "$work/tagged.webp"
        "$exe" -get xmp "$work/tagged.webp" -o "$work/recovered.xmp"
        busy cmp -s "$work/metadata.xmp" "$work/recovered.xmp" ;;
      zopfli)
        exe=$(original_elf /usr/bin/zopfli)
        busy printf 'NaSeed M132 exact gzip round trip: AAAABBBBCCCCDDDD\n' > "$work/input"
        "$exe" "$work/input"
        [ -s "$work/input.gz" ]
        busy gunzip -c "$work/input.gz" > "$work/decoded"
        busy cmp -s "$work/input" "$work/decoded" ;;
      zopflipng)
        exe=$(original_elf /usr/bin/zopflipng)
        seed_image
        "$exe" --iterations=1 --always_zopflify \
            "$work/frame.png" "$work/optimized.png"
        [ -s "$work/optimized.png" ]
        /usr/bin/cwebp -quiet -lossless "$work/optimized.png" -o "$work/optimized.webp"
        /usr/bin/dwebp -quiet "$work/optimized.webp" -ppm -o "$work/optimized.ppm"
        /usr/bin/dwebp -quiet "$work/source.webp" -ppm -o "$work/original.ppm"
        busy cmp -s "$work/original.ppm" "$work/optimized.ppm" ;;
      pcre2test)
        exe=$(original_elf /usr/bin/pcre2test)
        busy printf '/([a-z]+)-([0-9]+)/\nalpine-132\n\n' > "$work/expressions"
        "$exe" "$work/expressions" > "$work/matches"
        busy grep -Fq '1: alpine' "$work/matches"
        busy grep -Fq '2: 132' "$work/matches" ;;
      bzip3)
        exe=$(original_elf /usr/bin/bzip3)
        busy printf 'NaSeed M132 binary-safe BZip3 roundtrip: aaaabbbbccc\n' > "$work/input"
        "$exe" -b 1 -c "$work/input" > "$work/archive.bz3"
        [ -s "$work/archive.bz3" ]
        "$exe" -dc "$work/archive.bz3" > "$work/decoded"
        busy cmp -s "$work/input" "$work/decoded" ;;
      *) echo "[M132 UNKNOWN ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M132_TRACE:-0}" = 1 ]; then
        PS4='[M132 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in uconv genrb derb genccode gencmn icupkg makeconv \
           gif2webp img2webp webpinfo webpmux zopfli zopflipng pcre2test bzip3; do
    echo "[M132 CASE START] $app"
    case_log="/tmp/m132-$app-case.log"
    if M132_TRACE=1 /bin/sh /tests/m132-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M132_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M132 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -120 "$case_log" >&2 || :
        for detail in error info.txt dumped.txt contents.txt matches \
                      from-gif.webp animation.webp output.txt; do
            if [ -s "/tmp/m132-$app/$detail" ]; then
                echo "[M132 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 400 "/tmp/m132-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M132_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M132 BATCH FAIL] $failed/15 original ELFs (complete census)" >&2
    echo M132_BATCH_FAIL
    exit 1
fi
echo M132_BATCH_PASS
