#!/bin/sh
# Each signed, unmodified Alpine ELF must execute meaningful fixture data.
# A correct exit status alone, --help, or --version never counts as a pass.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C.UTF-8
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
original_elf() {
    executable=$1
    [ -x "$executable" ] || return 1
    resolved=$(busy readlink -f "$executable") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$executable"
}
nonempty() { [ -s "$work/$1" ]; }
contains() { busy grep -Fq "$2" "$work/$1"; }
is_png() {
    [ -s "$work/$1" ] || return 1
    sig=$(busy dd if="$work/$1" bs=1 count=8 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
    [ "$sig" = 89504e470d0a1a0a ]
}
case_run() {
    app=$1
    work="/tmp/m154-$app"
    busy mkdir -p "$work"
    exe=$(original_elf "/usr/bin/$app") || return 1
    case "$app" in
      a2ps)
        "$exe" -B -o "$work/out.ps" /tests/m154-data.txt > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%!PS-Adobe' && contains out.ps 'NaSeed' ;;
      barcode)
        "$exe" -e code128 -b NASEED154 -o "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%!PS' && contains out.ps 'NASEED154' ;;
      chafa)
        "$exe" --format symbols --colors none --size 16x16 /tests/m154-data.png > "$work/result" 2> "$work/error" || return 1
        nonempty result && [ "$(busy wc -c < "$work/result")" -ge 20 ] ;;
      chkfont)
        "$exe" /usr/share/figlet/fonts/standard.flf > "$work/result" 2> "$work/error" || return 1
        contains result 'Errors: 0' ;;
      csview)
        "$exe" /tests/m154-data.csv > "$work/result" 2> "$work/error" || return 1
        contains result Ada && contains result Lin && contains result score ;;
      csvq)
        busy cat /tests/m154-data.csv | "$exe" 'SELECT name FROM stdin WHERE integer(score)>8' > "$work/result" 2> "$work/error" || return 1
        contains result Lin && ! contains result Ada ;;
      demandoc)
        "$exe" /tests/m154-data.roff > "$work/result" 2> "$work/error" || return 1
        contains result 'original signed layout tool' ;;
      enca)
        busy cp /tests/m154-data.legacy "$work/input"
        "$exe" -L polish -x UTF-8 "$work/input" > "$work/result" 2> "$work/error" || return 1
        busy cmp -s "$work/input" /tests/m154-data.utf8 ;;
      enscript)
        "$exe" -B -p "$work/out.ps" /tests/m154-data.txt > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%!PS' && contains out.ps NaSeed ;;
      epsffit)
        "$exe" 0 0 200 200 /tests/m154-data.eps "$work/out.eps" > "$work/result" 2> "$work/error" || return 1
        contains out.eps '%%BoundingBox: 0 0 200 200' && nonempty out.eps ;;
      figlet)
        "$exe" -f standard NaSeed > "$work/result" 2> "$work/error" || return 1
        nonempty result && [ "$(busy wc -l < "$work/result")" -ge 3 ] ;;
      gnuplot)
        "$exe" -e "set terminal svg; set output '$work/plot.svg'; plot '/tests/m154-data.xy' using 1:2 with lines title 'NaSeed'" > "$work/result" 2> "$work/error" || return 1
        contains plot.svg '<svg' && contains plot.svg NaSeed ;;
      gs)
        "$exe" -q -dBATCH -dNOPAUSE -sDEVICE=txtwrite "-sOutputFile=$work/out.txt" /tests/m154-data.ps > "$work/result" 2> "$work/error" || return 1
        contains out.txt M154_PAGE_ONE && contains out.txt M154_PAGE_FOUR ;;
      html2text)
        "$exe" -utf8 /tests/m154-data.html > "$work/result" 2> "$work/error" || return 1
        contains result NaSeed && contains result 'HTML source converted' ;;
      jql)
        busy cat /tests/m154-data.json | "$exe" '"title"' > "$work/result" 2> "$work/error" || return 1
        contains result '"NaSeed"' ;;
      mandoc)
        "$exe" -Tascii /tests/m154-data.roff > "$work/result" 2> "$work/error" || return 1
        contains result 'original signed layout tool' ;;
      mkbitmap)
        "$exe" -f 1 -t 0.5 -o "$work/out.pbm" /tests/m154-data.pgm > "$work/result" 2> "$work/error" || return 1
        nonempty out.pbm && busy head -c 2 "$work/out.pbm" | busy grep -q P4 ;;
      pango-list)
        "$exe" > "$work/result" 2> "$work/error" || return 1
        contains result DejaVu ;;
      pango-segmentation)
        "$exe" --kind=word /tests/m154-data.txt > "$work/result" 2> "$work/error" || return 1
        nonempty result && (contains result NaSeed || contains result 'word') ;;
      pango-view)
        "$exe" --no-display --text='NaSeed M154' "--output=$work/out.png" > "$work/result" 2> "$work/error" || return 1
        is_png out.png ;;
      potrace)
        "$exe" -s -o "$work/out.svg" /tests/m154-data.pbm > "$work/result" 2> "$work/error" || return 1
        contains out.svg '<svg' ;;
      psbook)
        "$exe" /tests/m154-data.ps "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%%Page: four 1' && contains out.ps '%%Page: one 2' ;;
      psnup)
        "$exe" -2 /tests/m154-data.ps "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%%Pages: 2 0' && contains out.ps M154_PAGE_ONE && contains out.ps M154_PAGE_TWO ;;
      psresize)
        "$exe" -Pletter -pa4 /tests/m154-data.ps "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%%Page:' && contains out.ps M154_PAGE_TWO ;;
      psselect)
        "$exe" -p2 /tests/m154-data.ps "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps M154_PAGE_TWO && ! contains out.ps M154_PAGE_ONE ;;
      pstops)
        "$exe" '4:0,1,2,3' /tests/m154-data.ps "$work/out.ps" > "$work/result" 2> "$work/error" || return 1
        contains out.ps '%%Page:' && contains out.ps M154_PAGE_FOUR ;;
      rsvg-convert)
        "$exe" -f png -o "$work/out.png" /tests/m154-data.svg > "$work/result" 2> "$work/error" || return 1
        is_png out.png ;;
      taplo)
        "$exe" fmt - < /tests/m154-data.toml > "$work/result" 2> "$work/error" || return 1
        contains result 'title = "NaSeed"' && contains result 'answer = 42' ;;
      unrtf)
        "$exe" --text /tests/m154-data.rtf > "$work/result" 2> "$work/error" || return 1
        contains result 'NaSeed RTF converted' ;;
      xmlcatalog)
        "$exe" /tests/m154-data.catalog '-//NASEED//DTD M154//EN' > "$work/result" 2> "$work/error" || return 1
        contains result m154.dtd ;;
      *) echo "[M154 UNKNOWN ORIGINAL ELF] $app" >&2; return 2 ;;
    esac
}
if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in a2ps barcode chafa chkfont csview csvq demandoc enca enscript epsffit \
           figlet gnuplot gs html2text jql mandoc mkbitmap pango-list \
           pango-segmentation pango-view potrace psbook psnup psresize \
           psselect pstops rsvg-convert taplo unrtf xmlcatalog; do
    echo "[M154 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m154-$app-case.log"
    if /bin/sh /tests/m154-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M154_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M154 ORIGINAL ELF FAILED] $app rc=$rc" >&2
        busy tail -60 "$detail" >&2 || :
        for artifact in error result out.ps out.svg plot.svg out.txt; do
            if [ -s "/tmp/m154-$app/$artifact" ]; then
                echo "[M154 FAILURE DETAIL] $app/$artifact" >&2
                busy head -c 350 "/tmp/m154-$app/$artifact" >&2 || :
                echo >&2
            fi
        done
        printf 'M154_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M154 BATCH FAIL] $failures/30 authentic Alpine ELF semantic checks" >&2
    echo M154_BATCH_FAIL
    exit 1
fi
echo M154_BATCH_PASS
