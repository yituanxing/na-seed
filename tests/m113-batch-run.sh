#!/bin/sh
# M113: fifteen distinct ORIGINAL signed Alpine 3.23 native Linux executables.
# Linux host stages all APKs/dependencies. NaSeed ONLY runs real ELF programs
# on reproducible DATA: devicetrees, images, PDF, markdown and archives.
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
    echo "[M113 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

make_dts() {
    printf '/dts-v1/;\n/ {\n compatible = "naseed,m113";\n answer = <42>;\n};\n' > "$1"
}
make_dtb() {
    make_dts "$1.dts"
    dtc_encoder=$(original_elf /usr/bin/dtc)
    "$dtc_encoder" -I dts -O dtb -o "$1" "$1.dts"
    [ -s "$1" ]
}
make_ppm() {
    # Four exact pixels: red, green, blue, yellow. Deterministic DATA only.
    printf 'P6\n2 2\n255\n\377\000\000\000\377\000\000\000\377\377\377\000' > "$1"
}
make_tiff() {
    make_ppm "$1.ppm"
    tiff_encoder=$(original_elf /usr/bin/ppm2tiff)
    "$tiff_encoder" "$1.ppm" "$1"
    [ -s "$1" ]
}
make_pdf() {
    # 598-byte syntactically validated PDF with one extractable Helvetica line;
    # generated and independently decoded on Linux host before staging.
    printf '%s' 'JVBERi0xLjQKMSAwIG9iago8PCAvVHlwZSAvQ2F0YWxvZyAvUGFnZXMgMiAwIFIgPj4KZW5kb2JqCjIgMCBvYmoKPDwgL1R5cGUgL1BhZ2VzIC9LaWRzIFszIDAgUl0gL0NvdW50IDEgPj4KZW5kb2JqCjMgMCBvYmoKPDwgL1R5cGUgL1BhZ2UgL1BhcmVudCAyIDAgUiAvTWVkaWFCb3ggWzAgMCAzMDAgMjAwXSAvUmVzb3VyY2VzIDw8IC9Gb250IDw8IC9GMSA0IDAgUiA+PiA+PiAvQ29udGVudHMgNSAwIFIgPj4KZW5kb2JqCjQgMCBvYmoKPDwgL1R5cGUgL0ZvbnQgL1N1YnR5cGUgL1R5cGUxIC9CYXNlRm9udCAvSGVsdmV0aWNhID4+CmVuZG9iago1IDAgb2JqCjw8IC9MZW5ndGggNTUgPj4Kc3RyZWFtCkJUIC9GMSAxOCBUZiA0MCAxMDAgVGQgKE5hU2VlZCBNMTEzIFBERiBjb250ZW50KSBUaiBFVAplbmRzdHJlYW0KZW5kb2JqCnhyZWYKMCA2CjAwMDAwMDAwMDAgNjU1MzUgZiAKMDAwMDAwMDAwOSAwMDAwMCBuIAowMDAwMDAwMDU4IDAwMDAwIG4gCjAwMDAwMDAxMTUgMDAwMDAgbiAKMDAwMDAwMDI0MSAwMDAwMCBuIAowMDAwMDAwMzExIDAwMDAwIG4gCnRyYWlsZXIKPDwgL1NpemUgNiAvUm9vdCAxIDAgUiA+PgpzdGFydHhyZWYKNDE1CiUlRU9GCg==' | busy base64 -d > "$1"
    [ -s "$1" ]
}
make_markdown() {
    printf '# M113\n\n**NaSeed** supports *real* Linux tools.\n' > "$1"
}
check_ppm_pixels() {
    # PPM encoders may produce a different legal header. Verify dimensions
    # AND compare all 12 raw pixel bytes for an actual lossless roundtrip.
    busy head -n 1 "$1" | busy grep -Fxq P6
    busy head -n 2 "$1" | busy tail -n 1 | busy grep -Fxq '2 2'
    busy head -n 3 "$1" | busy tail -n 1 | busy grep -Fxq 255
    busy tail -c 12 "$1" > "$1.pixels"
    busy tail -c 12 "$2" > "$2.pixels"
    busy cmp "$1.pixels" "$2.pixels"
}

run_case() {
    app=$1
    work="/tmp/m113-$app"
    busy mkdir -p "$work"
    case "$app" in
      dtc)
        exe=$(original_elf /usr/bin/dtc)
        make_dts "$work/input.dts"
        "$exe" -I dts -O dtb -o "$work/tree.dtb" "$work/input.dts"
        [ -s "$work/tree.dtb" ]
        fdt_reader=$(original_elf /usr/bin/fdtget)
        "$fdt_reader" -t u "$work/tree.dtb" / answer > "$work/value"
        busy grep -Fxq 42 "$work/value" ;;
      fdtget)
        exe=$(original_elf /usr/bin/fdtget)
        make_dtb "$work/tree.dtb"
        "$exe" -t s "$work/tree.dtb" / compatible > "$work/value"
        busy grep -Fxq naseed,m113 "$work/value" ;;
      fdtput)
        exe=$(original_elf /usr/bin/fdtput)
        make_dtb "$work/tree.dtb"
        "$exe" -t s "$work/tree.dtb" / compatible naseed,done
        fdt_reader=$(original_elf /usr/bin/fdtget)
        "$fdt_reader" -t s "$work/tree.dtb" / compatible > "$work/value"
        busy grep -Fxq naseed,done "$work/value" ;;
      cwebp)
        exe=$(original_elf /usr/bin/cwebp)
        make_ppm "$work/source.ppm"
        "$exe" -lossless -quiet "$work/source.ppm" -o "$work/image.webp"
        [ -s "$work/image.webp" ]
        webp_reader=$(original_elf /usr/bin/dwebp)
        "$webp_reader" "$work/image.webp" -ppm -o "$work/roundtrip.ppm" >/dev/null 2>&1
        check_ppm_pixels "$work/roundtrip.ppm" "$work/source.ppm" ;;
      dwebp)
        exe=$(original_elf /usr/bin/dwebp)
        make_ppm "$work/source.ppm"
        webp_encoder=$(original_elf /usr/bin/cwebp)
        "$webp_encoder" -lossless -quiet "$work/source.ppm" -o "$work/image.webp"
        "$exe" "$work/image.webp" -ppm -o "$work/roundtrip.ppm" >/dev/null 2>&1
        check_ppm_pixels "$work/roundtrip.ppm" "$work/source.ppm" ;;
      ppm2tiff)
        exe=$(original_elf /usr/bin/ppm2tiff)
        make_ppm "$work/source.ppm"
        "$exe" "$work/source.ppm" "$work/image.tif"
        [ -s "$work/image.tif" ]
        tiff_reader=$(original_elf /usr/bin/tiffinfo)
        "$tiff_reader" "$work/image.tif" > "$work/info"
        busy grep -Fq 'Image Width: 2 Image Length: 2' "$work/info" ;;
      tiffinfo)
        exe=$(original_elf /usr/bin/tiffinfo)
        make_tiff "$work/image.tif"
        "$exe" "$work/image.tif" > "$work/info"
        busy grep -Fq 'Image Width: 2 Image Length: 2' "$work/info"
        busy grep -Fq 'Bits/Sample: 8' "$work/info" ;;
      tiffcp)
        exe=$(original_elf /usr/bin/tiffcp)
        make_tiff "$work/source.tif"
        "$exe" -c none "$work/source.tif" "$work/copied.tif"
        [ -s "$work/copied.tif" ]
        # TIFF containers and metadata need not match after recompression.
        # Upstream tiffcmp -t reports changed tags on stdout while comparing
        # every decoded pixel; its exit status detects real pixel differences.
        tiff_comparator=$(original_elf /usr/bin/tiffcmp)
        "$tiff_comparator" -t "$work/source.tif" "$work/copied.tif" > "$work/diff"
        ;;
      qpdf)
        exe=$(original_elf /usr/bin/qpdf)
        make_pdf "$work/source.pdf"
        "$exe" --check "$work/source.pdf" > "$work/source-check" 2>&1
        "$exe" --object-streams=disable "$work/source.pdf" "$work/rewritten.pdf"
        [ -s "$work/rewritten.pdf" ]
        "$exe" --check "$work/rewritten.pdf" > "$work/rewrite-check" 2>&1
        pdf_reader=$(original_elf /usr/bin/pdftotext)
        "$pdf_reader" "$work/rewritten.pdf" - > "$work/actual"
        busy grep -Fq 'NaSeed M113 PDF content' "$work/actual" ;;
      pdfinfo)
        exe=$(original_elf /usr/bin/pdfinfo)
        make_pdf "$work/source.pdf"
        "$exe" "$work/source.pdf" > "$work/info"
        busy grep -Eq '^Pages:[[:space:]]+1$' "$work/info"
        busy grep -Fq 'Page size: ' "$work/info" ;;
      pdftotext)
        exe=$(original_elf /usr/bin/pdftotext)
        make_pdf "$work/source.pdf"
        "$exe" "$work/source.pdf" - > "$work/actual"
        busy grep -Fxq 'NaSeed M113 PDF content' "$work/actual" ;;
      markdown)
        exe=$(original_elf /usr/bin/markdown)
        make_markdown "$work/source.md"
        "$exe" "$work/source.md" > "$work/output.html"
        busy grep -Fq '<strong>NaSeed</strong>' "$work/output.html"
        busy grep -Fq '<em>real</em>' "$work/output.html" ;;
      md2html)
        exe=$(original_elf /usr/bin/md2html)
        make_markdown "$work/source.md"
        "$exe" "$work/source.md" > "$work/output.html"
        busy grep -Fq '<strong>NaSeed</strong>' "$work/output.html"
        busy grep -Fq '<em>real</em>' "$work/output.html" ;;
      lzip)
        exe=$(original_elf /usr/bin/lzip)
        printf 'NaSeed M113 signed Linux ELF archive\n' > "$work/source"
        "$exe" -c "$work/source" > "$work/data.lz"
        [ -s "$work/data.lz" ]
        "$exe" -dc "$work/data.lz" > "$work/restored"
        busy cmp "$work/source" "$work/restored" ;;
      lzop)
        exe=$(original_elf /bin/lzop)
        printf 'NaSeed M113 original LZO executable\n' > "$work/source"
        "$exe" -c "$work/source" > "$work/data.lzo"
        [ -s "$work/data.lzo" ]
        "$exe" -dc "$work/data.lzo" > "$work/restored"
        busy cmp "$work/source" "$work/restored" ;;
      *)
        echo "[M113 UNKNOWN ORIGINAL ELF] $app" >&2
        return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m113-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in dtc fdtget fdtput cwebp dwebp ppm2tiff tiffinfo tiffcp \
           qpdf pdfinfo pdftotext markdown md2html lzip lzop; do
    echo "[M113 CASE START] $app"
    if /bin/sh /tests/m113-batch-run.sh --case "$app"; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M113_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M113 $upper FAILED] genuine Linux ELF or actual functional oracle, rc=$rc" >&2
        printf 'M113_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M113_BATCH_PASS
else
    echo "[M113 BATCH FAIL] $failed/15 original signed Linux executable checks" >&2
    echo M113_BATCH_FAIL
    exit 1
fi
