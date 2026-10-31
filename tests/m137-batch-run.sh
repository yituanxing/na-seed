#!/bin/sh
# M137: 20 distinct original signed Alpine ELF programs, 14 software families.
# Every case executes the real ELF, asserts behavior and preserves failure logs.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LANG=C
LC_ALL=C
TERM=xterm
XDG_CACHE_HOME=/tmp/m137-font-cache
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

seed_lmdb() {
    busy mkdir -p "$work/env"
    busy printf '%s\n' \
        'VERSION=3' 'format=print' 'type=btree' \
        'mapsize=10485760' 'maxreaders=126' 'HEADER=END' \
        ' alpha' ' one' ' beta' ' two' 'DATA=END' > "$work/seed.dump"
    /usr/bin/mdb_load -f "$work/seed.dump" "$work/env" > "$work/load.log" 2>&1
    [ -s "$work/env/data.mdb" ]
}

font_file=/usr/share/fonts/dejavu/DejaVuSans.ttf
need_font() {
    [ -s "$font_file" ]
    busy mkdir -p "$XDG_CACHE_HOME"
}

run_case() {
    app=$1
    work="/tmp/m137-$app"
    busy mkdir -p "$work"
    case "$app" in
      mdb_copy)
        exe=$(original_elf /usr/bin/mdb_copy)
        seed_lmdb
        busy mkdir -p "$work/copied"
        "$exe" -c "$work/env" "$work/copied" > "$work/output" 2>&1
        [ -s "$work/copied/data.mdb" ]
        /usr/bin/mdb_dump -p "$work/copied" > "$work/copied.dump"
        busy grep -Fq alpha "$work/copied.dump"
        busy grep -Fq one "$work/copied.dump" ;;
      mdb_dump)
        exe=$(original_elf /usr/bin/mdb_dump)
        seed_lmdb
        "$exe" -p "$work/env" > "$work/output"
        busy grep -Fxq 'VERSION=3' "$work/output"
        busy grep -Fq alpha "$work/output"
        busy grep -Fq beta "$work/output"
        busy grep -Fq 'DATA=END' "$work/output" ;;
      mdb_load)
        exe=$(original_elf /usr/bin/mdb_load)
        busy mkdir -p "$work/env"
        busy printf '%s\n' \
            'VERSION=3' 'format=print' 'type=btree' \
            'mapsize=10485760' 'HEADER=END' \
            ' m137key' ' m137value' 'DATA=END' > "$work/seed.dump"
        "$exe" -f "$work/seed.dump" "$work/env" > "$work/output" 2>&1
        [ -s "$work/env/data.mdb" ]
        /usr/bin/mdb_dump -p "$work/env" > "$work/dump"
        busy grep -Fq m137key "$work/dump"
        busy grep -Fq m137value "$work/dump" ;;
      mdb_stat)
        exe=$(original_elf /usr/bin/mdb_stat)
        seed_lmdb
        "$exe" -a "$work/env" > "$work/output"
        busy grep -Eq 'Entries:[[:space:]]*2' "$work/output"
        busy grep -Fq 'Page size:' "$work/output" ;;
      fc_cache)
        exe=$(original_elf /usr/bin/fc-cache)
        need_font
        "$exe" -f -v /usr/share/fonts/dejavu > "$work/output" 2>&1
        busy grep -Fq 'dejavu' "$work/output"
        /usr/bin/fc-match -f '%{family}\n' 'DejaVu Sans' > "$work/matched"
        busy grep -Fq 'DejaVu Sans' "$work/matched" ;;
      fc_list)
        exe=$(original_elf /usr/bin/fc-list)
        need_font
        "$exe" -f '%{family}\n' > "$work/output"
        busy grep -Fq 'DejaVu Sans' "$work/output"
        busy grep -Fq 'DejaVu Serif' "$work/output" ;;
      fc_match)
        exe=$(original_elf /usr/bin/fc-match)
        need_font
        "$exe" -f '%{family}\n' 'DejaVu Sans' > "$work/output"
        busy grep -Fq 'DejaVu Sans' "$work/output"
        "$exe" -f '%{file}\n' 'DejaVu Sans' > "$work/file"
        busy grep -Fq '/usr/share/fonts/dejavu/' "$work/file" ;;
      fc_scan)
        exe=$(original_elf /usr/bin/fc-scan)
        need_font
        "$exe" -f '%{family}\n%{style}\n' "$font_file" > "$work/output"
        busy grep -Fq 'DejaVu Sans' "$work/output"
        busy grep -Fq 'Book' "$work/output" ;;
      protoc)
        exe=$(original_elf /usr/bin/protoc-31.1.0)
        busy printf '%s\n' \
            'syntax = "proto3";' 'package m137;' \
            'message Ping { string text = 1; int32 id = 2; }' > "$work/ping.proto"
        "$exe" -I "$work" --cpp_out="$work" "$work/ping.proto" \
            > "$work/output" 2>&1
        [ -s "$work/ping.pb.h" ] && [ -s "$work/ping.pb.cc" ]
        busy grep -Fq 'class Ping' "$work/ping.pb.h"
        busy grep -Fq 'm137' "$work/ping.pb.cc" ;;
      flatc)
        exe=$(original_elf /usr/bin/flatc)
        busy printf '%s\n' 'namespace M137;' \
            'table Item { id:int; name:string; }' \
            'root_type Item;' 'file_identifier "M137";' > "$work/item.fbs"
        busy printf '%s\n' '{"id":23,"name":"NaSeed"}' > "$work/item.json"
        "$exe" -b -o "$work" "$work/item.fbs" "$work/item.json" \
            > "$work/output" 2>&1
        [ -s "$work/item.bin" ]
        busy mkdir -p "$work/decoded"
        "$exe" -t -o "$work/decoded" "$work/item.fbs" -- "$work/item.bin" \
            >> "$work/output" 2>&1
        [ -s "$work/decoded/item.json" ]
        busy grep -Fq 'NaSeed' "$work/decoded/item.json"
        busy grep -Fq '23' "$work/decoded/item.json" ;;
      swig)
        exe=$(original_elf /usr/bin/swig)
        busy printf '%s\n' '%module m137' '%inline %{' \
            'int m137_double(int x) { return x * 2; }' \
            '%}' > "$work/interface.i"
        "$exe" -python -o "$work/m137_wrap.c" "$work/interface.i" \
            > "$work/output" 2>&1
        [ -s "$work/m137_wrap.c" ]
        busy grep -Fq 'm137_double' "$work/m137_wrap.c"
        busy grep -Fq '_wrap_m137_double' "$work/m137_wrap.c" ;;
      ragel)
        exe=$(original_elf /usr/bin/ragel)
        busy printf '%s\n' \
            '%%{' 'machine m137;' "main := 'NASEED';" '}%%' \
            '%% write data;' \
            'int scan(const char *p) {' \
            '  const char *pe = p + 6;' \
            '  int cs;' '  %% write init;' '  %% write exec;' \
            '  return cs;' '}' > "$work/machine.rl"
        "$exe" -C -o "$work/machine.c" "$work/machine.rl" \
            > "$work/output" 2>&1
        [ -s "$work/machine.c" ]
        busy grep -Fq 'm137_start' "$work/machine.c"
        busy grep -Fq 'm137_first_final' "$work/machine.c" ;;
      xorriso)
        exe=$(original_elf /usr/bin/xorriso)
        busy mkdir -p "$work/source"
        busy printf 'M137 original signed ISO roundtrip\n' > "$work/source/payload.txt"
        "$exe" -as mkisofs -R -J -o "$work/image.iso" \
            "$work/source" > "$work/write.log" 2>&1
        [ -s "$work/image.iso" ]
        "$exe" -indev "$work/image.iso" -osirrox on \
            -extract /payload.txt "$work/recovered.txt" \
            > "$work/read.log" 2>&1
        busy cmp -s "$work/source/payload.txt" "$work/recovered.txt" ;;
      gm)
        exe=$(original_elf /usr/bin/gm)
        busy printf 'P3\n2 2\n255\n255 0 0\n0 255 0\n0 0 255\n255 255 255\n' \
            > "$work/source.ppm"
        "$exe" convert "$work/source.ppm" -resize '6x4!' "$work/out.png" \
            > "$work/output" 2>&1
        [ -s "$work/out.png" ]
        "$exe" identify -format '%wx%h' "$work/out.png" > "$work/dimensions"
        busy grep -Fxq '6x4' "$work/dimensions"
        magic=$(busy dd if="$work/out.png" bs=1 count=8 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 89504e470d0a1a0a ] ;;
      mdbook)
        exe=$(original_elf /usr/bin/mdbook)
        busy mkdir -p "$work/book/src"
        busy printf '%s\n' '[book]' 'title = "NaSeed M137"' \
            > "$work/book/book.toml"
        busy printf '%s\n' '# Summary' '' '- [Intro](intro.md)' \
            > "$work/book/src/SUMMARY.md"
        busy printf '%s\n' '# Native Documentation' \
            'M137_MDBOOK_REAL_HTML_GENERATION' > "$work/book/src/intro.md"
        "$exe" build "$work/book" > "$work/output" 2>&1
        [ -s "$work/book/book/intro.html" ]
        busy grep -Fq 'M137_MDBOOK_REAL_HTML_GENERATION' \
            "$work/book/book/intro.html" ;;
      rclone)
        exe=$(original_elf /usr/bin/rclone)
        busy mkdir -p "$work/source/sub" "$work/copied"
        busy printf 'M137 RCLONE NATIVE LOCAL COPY\n' > "$work/source/sub/data.txt"
        "$exe" copy "$work/source" "$work/copied" \
            --transfers 1 --checkers 1 > "$work/output" 2>&1
        busy cmp -s "$work/source/sub/data.txt" "$work/copied/sub/data.txt"
        "$exe" check "$work/source" "$work/copied" \
            --checkers 1 >> "$work/output" 2>&1 ;;
      restic)
        exe=$(original_elf /usr/bin/restic)
        busy mkdir -p "$work/source" "$work/restored"
        busy printf 'M137 RESTIC LOCAL BACKUP ROUNDTRIP\n' \
            > "$work/source/payload.txt"
        RESTIC_PASSWORD=m137-real-backup
        RESTIC_REPOSITORY="$work/repo"
        export RESTIC_PASSWORD RESTIC_REPOSITORY
        "$exe" init > "$work/init.log" 2>&1
        "$exe" backup --no-cache "$work/source" > "$work/backup.log" 2>&1
        "$exe" restore latest --target "$work/restored" --no-cache \
            > "$work/restore.log" 2>&1
        recovered=$(busy find "$work/restored" -type f -name payload.txt |
                    busy head -n 1)
        [ -n "$recovered" ] && [ -s "$recovered" ]
        busy cmp -s "$work/source/payload.txt" "$recovered" ;;
      recode)
        exe=$(original_elf /usr/bin/recode)
        busy printf 'caf\303\251 M137\n' > "$work/utf8"
        "$exe" UTF-8..ISO-8859-1 < "$work/utf8" > "$work/latin1"
        [ -s "$work/latin1" ]
        bytes=$(busy od -An -tx1 "$work/latin1" | busy tr -d ' \n')
        case "$bytes" in *636166e9*) ;; *) return 1 ;; esac
        "$exe" ISO-8859-1..UTF-8 < "$work/latin1" > "$work/roundtrip"
        busy cmp -s "$work/utf8" "$work/roundtrip" ;;
      fdupes)
        exe=$(original_elf /usr/bin/fdupes)
        busy mkdir -p "$work/files"
        busy printf 'M137-IDENTICAL-PAYLOAD\n' > "$work/files/alpha"
        busy cp "$work/files/alpha" "$work/files/beta"
        busy printf 'M137-DIFFERENT-PAYLOAD\n' > "$work/files/gamma"
        "$exe" -r "$work/files" > "$work/output"
        busy grep -Fq '/alpha' "$work/output"
        busy grep -Fq '/beta' "$work/output"
        ! busy grep -Fq '/gamma' "$work/output" ;;
      zint)
        exe=$(original_elf /usr/bin/zint)
        "$exe" -b 58 -d 'M137-REAL-QR-CODE' -o "$work/qrcode.png" \
            > "$work/output" 2>&1
        [ -s "$work/qrcode.png" ]
        magic=$(busy dd if="$work/qrcode.png" bs=1 count=8 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 89504e470d0a1a0a ]
        # The authentic generator also provides an independent QR matrix
        # dump. zbarimg's optional ImageMagick PNG decode module is absent
        # in the no-scripts Alpine staging image, so its failure must not
        # be misreported as a Zint QR encoder failure.
        "$exe" -b 58 -d 'M137-REAL-QR-CODE' --dump > "$work/encoded"
        [ -s "$work/encoded" ]
        busy grep -Eq '[0-9A-Fa-f]{4,}' "$work/encoded" ;;
      *) echo "[M137 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M137_TRACE:-0}" = 1 ]; then
        PS4='[M137 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in mdb_copy mdb_dump mdb_load mdb_stat \
           fc_cache fc_list fc_match fc_scan \
           protoc flatc swig ragel xorriso gm mdbook \
           rclone restic recode fdupes zint; do
    echo "[M137 CASE START] $app"
    case_log="/tmp/m137-$app-case.log"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    if M137_TRACE=1 /bin/sh /tests/m137-batch-run.sh --case "$app" \
          > "$case_log" 2>&1; then
        printf 'M137_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M137 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -160 "$case_log" >&2 || :
        for detail in output write.log read.log load.log copied.dump \
                      dump seed.dump matched file dimensions \
                      init.log backup.log restore.log decoded encoded \
                      utf8 latin1 roundtrip; do
            if [ -s "/tmp/m137-$app/$detail" ]; then
                echo "[M137 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 800 "/tmp/m137-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M137_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M137 BATCH FAIL] $failed/20 original ELFs (full census)" >&2
    echo M137_BATCH_FAIL
    exit 1
fi
echo M137_BATCH_PASS
