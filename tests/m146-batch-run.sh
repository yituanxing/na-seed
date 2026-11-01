#!/bin/sh
# M146: twenty four independent functional checks of ORIGINAL signed Alpine
# executables, from five APKs, in the native NaSeed QEMU userspace.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }

original_elf() {
    executable=$1
    [ -x "$executable" ] || return 1
    resolved=$(busy readlink -f "$executable") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$executable"
}
seed_images() {
    busy printf '%s\n' 'P3' '4 4' '255' \
        '255 0 0  0 255 0  0 0 255  255 255 255' \
        '0 0 0  128 64 32  32 64 128  255 0 255' \
        '255 255 0  10 20 30  30 20 10  0 255 255' \
        '1 2 3  16 32 48  96 80 64  220 210 200' > "$work/source.ppm"
    busy printf '%s\n' 'P1' '8 8' \
        '0 0 0 0 0 0 0 0' '0 1 1 0 0 1 1 0' \
        '0 1 0 1 1 0 1 0' '0 0 1 0 0 1 0 0' \
        '0 0 1 0 0 1 0 0' '0 1 0 1 1 0 1 0' \
        '0 1 1 0 0 1 1 0' '0 0 0 0 0 0 0 0' > "$work/source.pbm"
}
image_dimensions() {
    [ -s "$work/result.pnm" ] || return 1
    /usr/bin/pamfile "$work/result.pnm" > "$work/geometry" 2>&1 || return 1
    busy grep -Fq "$1 by $2" "$work/geometry"
}
assert_magic() {
    [ -s "$work/result" ] || return 1
    bytes=$(busy dd if="$work/result" bs=1 count="$2" 2>/dev/null)
    [ "$bytes" = "$1" ]
}
run_case() {
    app=$1
    work="/tmp/m146-$app"
    busy mkdir -p "$work"
    case "$app" in
      magick)
        exe=$(original_elf /usr/bin/magick)
        "$exe" -size 3x2 xc:navy txt:- > "$work/result" 2> "$work/error"
        busy grep -Fq '3,2' "$work/result"
        busy grep -Fq 'navy' "$work/result" ;;
      php84)
        exe=$(original_elf /usr/bin/php84)
        "$exe" -n -r 'echo implode(",", array_map(fn($x) => $x*$x, [1,2,3]));' \
            > "$work/result" 2> "$work/error"
        busy grep -Fxq '1,4,9' "$work/result" ;;
      dumpelf)
        exe=$(original_elf /usr/bin/dumpelf)
        "$exe" /usr/bin/php84 > "$work/result" 2> "$work/error"
        [ -s "$work/result" ] ;;
      scanelf)
        exe=$(original_elf /usr/bin/scanelf)
        "$exe" -n /usr/bin/php84 > "$work/result" 2> "$work/error"
        busy grep -Fq 'libc.musl' "$work/result" ;;
      *)
        exe=$(original_elf "/usr/bin/$app")
        seed_images
        case "$app" in
          pamtofits)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            assert_magic SIMPLE 6 ;;
          pamtogif)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            assert_magic GIF 3 ;;
          pamtohtmltbl)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            busy grep -iq '<table' "$work/result" ;;
          pamtosvg)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            busy grep -iq '<svg' "$work/result" ;;
          pamtotga|pamtoxvmini|pnmtorle)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            [ -s "$work/result" ] ;;
          pamtouil)
            # The native converter needs a real X11-format RGB color-name
            # dictionary. Alpine's no-scripts APK overlay has no X11 data
            # package; give it explicit valid color metadata for our PPM.
            busy printf '%s\n' '0 0 0 black' '255 255 255 white' \
                '255 0 0 red' '0 255 0 green' '0 0 255 blue' \
                '255 255 0 yellow' '255 0 255 magenta' '0 255 255 cyan' \
                > "$work/rgb.txt"
            RGBDEF="$work/rgb.txt" "$exe" "$work/source.ppm" \
                > "$work/result" 2> "$work/error"
            [ -s "$work/result" ] ;;
          pbmtoascii)
            "$exe" "$work/source.pbm" > "$work/result" 2> "$work/error"
            # Netpbm's default 1x2 mapping produces FOUR text rows from an
            # eight-pixel-high PBM, not eight. Assert the actual heart motif.
            busy printf '%s\n' ' oo  oo' ' "o""o"' ' o"oo"o' ' ""  ""' \
                > "$work/expected"
            busy cmp -s "$work/result" "$work/expected" ;;
          pbmtopgm)
            # pbmtopgm performs convolution; its odd kernel dimensions are
            # mandatory arguments, followed by the original bitmap path.
            "$exe" 3 3 "$work/source.pbm" \
                > "$work/result.pnm" 2> "$work/error"
            image_dimensions 8 8 ;;
          pbmtosunicon)
            "$exe" "$work/source.pbm" > "$work/result" 2> "$work/error"
            # Its genuine Sun icon syntax is a descriptor comment plus hex
            # pixel words, not the '#define/static' C style of XBM.
            busy grep -Fq 'Format_version=1, Width=16, Height=8' "$work/result"
            busy grep -Fq '0x0660' "$work/result" ;;
          pbmtoxbm)
            "$exe" "$work/source.pbm" > "$work/result" 2> "$work/error"
            busy grep -Eq '(#define|static)' "$work/result" ;;
          pnmtopalm)
            # Arbitrary 24-bit PPM colors are not in Palm's fixed 8-bit
            # colormap; native 16-bit direct color supports the original PPM.
            "$exe" -depth=16 "$work/source.ppm" \
                > "$work/result" 2> "$work/error"
            [ -s "$work/result" ]
            header=$(busy dd if="$work/result" bs=1 count=4 2>/dev/null |
                     busy od -An -tx1 | busy tr -d ' \n')
            [ "$header" = 00040004 ] ;;
          pbmtoepsi)
            "$exe" "$work/source.pbm" > "$work/result" 2> "$work/error"
            assert_magic '%!PS' 4 ;;
          pnmtops)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            assert_magic '%!PS' 4 ;;
          pnmtosgi)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            magic=$(busy dd if="$work/result" bs=1 count=2 2>/dev/null |
                    busy od -An -tx1 | busy tr -d ' \n')
            [ "$magic" = 01da ] ;;
          pnmtotiffcmyk)
            "$exe" "$work/source.ppm" > "$work/result" 2> "$work/error"
            magic=$(busy dd if="$work/result" bs=1 count=2 2>/dev/null |
                    busy od -An -tx1 | busy tr -d ' \n')
            case "$magic" in 4949|4d4d) ;; *) return 1 ;; esac ;;
          pamenlarge|pamstretch)
            "$exe" 2 "$work/source.ppm" > "$work/result.pnm" 2> "$work/error"
            image_dimensions 8 8 ;;
          pamstack)
            "$exe" "$work/source.ppm" "$work/source.ppm" \
                > "$work/result.pnm" 2> "$work/error"
            image_dimensions 4 4 ;;
          *) echo "[M146 UNKNOWN ORIGINAL ELF] $app" >&2; return 2 ;;
        esac ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in pamtofits pamtogif pamtohtmltbl pamtosvg pamtotga pamtouil \
           pamtoxvmini pbmtoascii pbmtopgm pbmtosunicon pbmtoxbm pbmtoepsi \
           pnmtopalm pnmtops pnmtosgi pnmtorle pnmtotiffcmyk pamenlarge \
           pamstretch pamstack magick php84 dumpelf scanelf; do
    echo "[M146 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    detail="/tmp/m146-$app-case.log"
    if /bin/sh /tests/m146-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M146_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M146 ORIGINAL ELF FAILED] $app rc=$rc" >&2
        busy tail -100 "$detail" >&2 || :
        for artifact in error result geometry result.pnm; do
            if [ -s "/tmp/m146-$app/$artifact" ]; then
                echo "[M146 FAILURE DETAIL] $app/$artifact" >&2
                busy head -c 512 "/tmp/m146-$app/$artifact" >&2 || :
                echo >&2
            fi
        done
        printf 'M146_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M146 BATCH FAIL] $failed/24 original signed ELF semantic checks" >&2
    echo M146_BATCH_FAIL
    exit 1
fi
echo M146_BATCH_PASS
