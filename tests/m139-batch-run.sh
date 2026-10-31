#!/bin/sh
# M139: twenty new, independently checked original signed Alpine ELF programs.
# Input, transformation and output are verified inside a real NaSeed guest.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LANG=C
LC_ALL=C
TERM=xterm
XDG_CACHE_HOME=/tmp/m139-font-cache
export PATH HOME LANG LC_ALL TERM XDG_CACHE_HOME
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

font_file=/usr/share/fonts/dejavu/DejaVuSans.ttf
need_font() {
    [ -s "$font_file" ]
    busy mkdir -p "$XDG_CACHE_HOME"
}

seed_ppm() {
    busy printf '%s\n' \
        'P3' '4 3' '255' \
        '255 0 0   0 255 0   0 0 255   255 255 255' \
        '0 0 0     255 0 0   0 255 0   0 0 255' \
        '255 255 0 255 0 255 0 255 255 128 128 128' > "$work/source.ppm"
}

run_case() {
    app=$1
    work="/tmp/m139-$app"
    busy mkdir -p "$work"
    case "$app" in
      fc_cat)
        exe=$(original_elf /usr/bin/fc-cat)
        need_font
        /usr/bin/fc-cache -f /usr/share/fonts/dejavu > "$work/cache.log" 2>&1
        "$exe" -v /usr/share/fonts/dejavu > "$work/output" 2>&1
        busy grep -Fq 'DejaVu' "$work/output"
        busy grep -Fq 'family' "$work/output" ;;
      fc_conflist)
        exe=$(original_elf /usr/bin/fc-conflist)
        need_font
        "$exe" > "$work/output" 2>&1
        busy grep -Fq 'fonts.conf' "$work/output"
        busy grep -Fq 'conf.d' "$work/output" ;;
      fc_pattern)
        exe=$(original_elf /usr/bin/fc-pattern)
        need_font
        "$exe" -f '%{family}\n%{style}\n' 'DejaVu Sans:style=Book' \
            > "$work/output" 2>&1
        busy grep -Fxq 'DejaVu Sans' "$work/output"
        busy grep -Fxq 'Book' "$work/output" ;;
      fc_query)
        exe=$(original_elf /usr/bin/fc-query)
        need_font
        "$exe" -f '%{family}\n%{style}\n%{fontversion}\n' \
            "$font_file" > "$work/output" 2>&1
        busy grep -Fq 'DejaVu Sans' "$work/output"
        busy grep -Fq 'Book' "$work/output"
        [ "$(busy wc -l < "$work/output")" -ge 3 ] ;;
      fc_validate)
        exe=$(original_elf /usr/bin/fc-validate)
        need_font
        "$exe" "$font_file" > "$work/output" 2>&1
        busy grep -Fq 'valid' "$work/output"
        busy grep -Fq 'DejaVuSans.ttf' "$work/output" ;;
      pnmpad)
        exe=$(original_elf /usr/bin/pnmpad)
        seed_ppm
        "$exe" -white -left=2 -right=1 -top=1 -bottom=1 \
            "$work/source.ppm" > "$work/result.ppm" 2> "$work/output"
        [ -s "$work/result.ppm" ]
        /usr/bin/pamfile "$work/result.ppm" > "$work/geometry"
        busy grep -Fq '7 by 5' "$work/geometry" ;;
      pnmconvol)
        exe=$(original_elf /usr/bin/pnmconvol)
        seed_ppm
        busy printf '%s\n' 'P3' '3 3' '255' \
            '128 128 128  128 128 128  128 128 128' \
            '128 128 128  255 255 255  128 128 128' \
            '128 128 128  128 128 128  128 128 128' > "$work/kernel.ppm"
        "$exe" "$work/kernel.ppm" "$work/source.ppm" \
            > "$work/result.ppm" 2> "$work/output"
        [ -s "$work/result.ppm" ]
        /usr/bin/pamfile "$work/result.ppm" > "$work/geometry"
        busy grep -Fq '4 by 3' "$work/geometry" ;;
      pnmhisteq)
        exe=$(original_elf /usr/bin/pnmhisteq)
        busy printf '%s\n' 'P2' '4 2' '255' \
            '0 0 0 0' '0 5 80 255' > "$work/source.pgm"
        "$exe" "$work/source.pgm" > "$work/result.pgm" 2> "$work/output"
        [ -s "$work/result.pgm" ]
        /usr/bin/pamfile "$work/result.pgm" > "$work/geometry"
        busy grep -Fq '4 by 2' "$work/geometry" ;;
      ppmtoxpm)
        exe=$(original_elf /usr/bin/ppmtoxpm)
        seed_ppm
        "$exe" "$work/source.ppm" > "$work/result.xpm" 2> "$work/output"
        [ -s "$work/result.xpm" ]
        busy grep -Fq '/* XPM */' "$work/result.xpm"
        busy grep -Fq '4 3 ' "$work/result.xpm" ;;
      ppmtotga)
        exe=$(original_elf /usr/bin/ppmtotga)
        seed_ppm
        "$exe" "$work/source.ppm" > "$work/result.tga" 2> "$work/output"
        [ -s "$work/result.tga" ]
        /usr/bin/tgatoppm "$work/result.tga" > "$work/roundtrip.ppm"
        /usr/bin/pnmtoplainpnm "$work/source.ppm" > "$work/source.plain"
        /usr/bin/pnmtoplainpnm "$work/roundtrip.ppm" > "$work/roundtrip.plain"
        busy cmp -s "$work/source.plain" "$work/roundtrip.plain" ;;
      tgatoppm)
        exe=$(original_elf /usr/bin/tgatoppm)
        seed_ppm
        /usr/bin/ppmtotga "$work/source.ppm" > "$work/source.tga"
        "$exe" "$work/source.tga" > "$work/result.ppm" 2> "$work/output"
        [ -s "$work/result.ppm" ]
        /usr/bin/pnmtoplainpnm "$work/source.ppm" > "$work/source.plain"
        /usr/bin/pnmtoplainpnm "$work/result.ppm" > "$work/result.plain"
        busy cmp -s "$work/source.plain" "$work/result.plain" ;;
      chrt)
        exe=$(original_elf /usr/bin/chrt)
        "$exe" -p "$$" > "$work/output" 2>&1
        busy grep -Fq 'scheduling policy' "$work/output"
        busy grep -Fq 'SCHED_OTHER' "$work/output" ;;
      setsid)
        exe=$(original_elf /usr/bin/setsid)
        "$exe" -w /bin/sh -c \
            "printf '%s\\n' M139_NEW_SESSION > '$work/session.txt'" \
            > "$work/output" 2>&1
        busy grep -Fxq 'M139_NEW_SESSION' "$work/session.txt" ;;
      mountpoint)
        exe=$(original_elf /usr/bin/mountpoint)
        "$exe" -q / > "$work/output" 2>&1
        busy mkdir -p "$work/ordinary-directory"
        if "$exe" -q "$work/ordinary-directory" >> "$work/output" 2>&1; then
            return 1
        fi ;;
      fincore)
        exe=$(original_elf /usr/bin/fincore)
        busy dd if=/dev/zero of="$work/data" bs=4096 count=2 2> "$work/seed.log"
        [ "$(busy wc -c < "$work/data")" -eq 8192 ]
        "$exe" --noheadings --output PAGES,SIZE "$work/data" \
            > "$work/output" 2>&1
        busy grep -Eq '[12][[:space:]]+8192' "$work/output" ;;
      ul)
        exe=$(original_elf /usr/bin/ul)
        busy printf 'M139_\b_M139_UNDERLINE\n' > "$work/overstrike"
        "$exe" -t xterm < "$work/overstrike" > "$work/output" 2> "$work/error"
        [ -s "$work/output" ]
        busy grep -Fq 'M139' "$work/output" ;;
      mcookie)
        exe=$(original_elf /usr/bin/mcookie)
        "$exe" > "$work/output" 2> "$work/error"
        "$exe" > "$work/second" 2>> "$work/error"
        busy grep -Eq '^[0123456789abcdefABCDEF]{32}$' "$work/output"
        busy grep -Eq '^[0123456789abcdefABCDEF]{32}$' "$work/second"
        ! busy cmp -s "$work/output" "$work/second" ;;
      certtool)
        exe=$(original_elf /usr/bin/certtool)
        "$exe" --generate-privkey --key-type ecdsa --curve secp256r1 \
            --outfile "$work/private.pem" > "$work/output" 2>&1
        [ -s "$work/private.pem" ]
        busy grep -Fq -- '-----BEGIN' "$work/private.pem"
        busy grep -Fq -- 'PRIVATE KEY-----' "$work/private.pem" ;;
      psktool)
        exe=$(original_elf /usr/bin/psktool)
        "$exe" --username m139-guest --keysize 32 \
            --pskfile "$work/psk.txt" > "$work/output" 2>&1
        [ -s "$work/psk.txt" ]
        busy grep -Eq '^m139-guest:[[:xdigit:]]{64}$' "$work/psk.txt" ;;
      gpgconf)
        exe=$(original_elf /usr/bin/gpgconf)
        GNUPGHOME="$work/gnupg"
        export GNUPGHOME
        busy mkdir -p "$GNUPGHOME"
        "$exe" --list-dirs > "$work/output" 2>&1
        busy grep -Fq 'homedir:' "$work/output"
        busy grep -Fq "$GNUPGHOME" "$work/output"
        "$exe" --list-components > "$work/components" 2>&1
        busy grep -Eq '^gpg:' "$work/components" ;;
      *) echo "[M139 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M139_TRACE:-0}" = 1 ]; then
        PS4='[M139 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in fc_cat fc_conflist fc_pattern fc_query fc_validate \
           pnmpad pnmconvol pnmhisteq ppmtoxpm ppmtotga tgatoppm \
           chrt setsid mountpoint fincore ul mcookie certtool psktool gpgconf; do
    echo "[M139 CASE START] $app"
    case_log="/tmp/m139-$app-case.log"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    if M139_TRACE=1 /bin/sh /tests/m139-batch-run.sh --case "$app" \
          > "$case_log" 2>&1; then
        printf 'M139_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M139 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -160 "$case_log" >&2 || :
        for detail in output error cache.log geometry source.ppm source.pgm \
                      result.ppm result.pgm result.xpm result.tga result.plain \
                      source.tga source.plain roundtrip.ppm roundtrip.plain \
                      kernel.ppm session.txt seed.log overstrike data second \
                      private.pem psk.txt components; do
            if [ -s "/tmp/m139-$app/$detail" ]; then
                echo "[M139 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 800 "/tmp/m139-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M139_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M139 BATCH FAIL] $failed/20 original ELFs (full census)" >&2
    echo M139_BATCH_FAIL
    exit 1
fi
echo M139_BATCH_PASS
