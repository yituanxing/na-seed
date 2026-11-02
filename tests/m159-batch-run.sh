#!/bin/sh
# 8 new signed Subversion, 5 NSS and 17 Netpbm executables. Every case
# performs an actual operation on repository, certificate or image data.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
RGBDEF=/tests/m159-rgb.txt
export PATH HOME LC_ALL RGBDEF
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
image_check() {
    image=$1
    work=$2
    [ -s "$image" ] || return 1
    magic=$(busy dd if="$image" bs=1 count=2 2>/dev/null)
    case "$magic" in P[1-7]) ;; *) return 1 ;; esac
    /usr/bin/pamfile "$image" > "$work/pamfile.log" 2>&1 || return 1
    busy grep -qE '[0-9]+ by [0-9]+' "$work/pamfile.log" || return 1
    # Independent original parser consumes real nontrivial decoded pixels.
    /usr/bin/pamcut -left 0 -top 0 -width 8 -height 8 "$image" \
        > "$work/crop.pam" 2> "$work/crop.error" || return 1
    /usr/bin/pamtable "$work/crop.pam" > "$work/pixels.txt" 2>&1 || return 1
    [ "$(busy wc -l < "$work/pixels.txt")" -eq 8 ] || return 1
    busy grep -qE '[1-9]' "$work/pixels.txt"
}
make_svn() {
    repo="$work/repo"
    url="file://$repo"
    /usr/bin/svnadmin create "$repo" > "$work/create.log" 2>&1 || return 1
    /usr/bin/svn import /tests/m159-svn-note.txt "$url/note.txt" \
        -m m159-seed --non-interactive > "$work/import.log" 2>&1 || return 1
    /usr/bin/svnlook tree "$repo" > "$work/tree.log" 2>&1 || return 1
    busy grep -Fq note.txt "$work/tree.log"
}
make_nss() {
    db="$work/nssdb"
    busy mkdir -p "$db"
    /usr/bin/certutil -N -d "sql:$db" --empty-password \
        > "$work/nss-create.log" 2>&1 || return 1
    /usr/bin/pk12util -i /tests/m159-test-cert.p12 \
        -d "sql:$db" -W m159-fixture -K '' \
        > "$work/nss-import.log" 2>&1 || return 1
    /usr/bin/certutil -L -d "sql:$db" \
        > "$work/nss-list.log" 2>&1 || return 1
    busy grep -Fq m159-nss-fixture "$work/nss-list.log"
}
case_run() {
    app=$1
    work="/tmp/m159-$app"
    busy mkdir -p "$work"
    exe=$(original "$app") || return 1
    src=/tests/m159-color.ppm
    gray=/tests/m159-gray.pgm
    bw=/tests/m159-bw.pbm
    out="$work/out.pnm"
    case "$app" in
      svn)
        make_svn || return 1
        "$exe" checkout "$url" "$work/checkout" --non-interactive \
            > "$work/result" 2> "$work/error" || return 1
        busy cmp -s "$work/checkout/note.txt" /tests/m159-svn-note.txt &&
        "$exe" log "$work/checkout" > "$work/log" 2>&1 &&
        busy grep -Fq m159-seed "$work/log" ;;
      svnadmin)
        make_svn || return 1
        "$exe" verify "$repo" > "$work/result" 2> "$work/error" || return 1
        "$exe" dump "$repo" > "$work/dump" 2> "$work/dump-error" || return 1
        busy grep -Fq 'Node-path: note.txt' "$work/dump" &&
        busy grep -Fq 'SVN-fs-dump-format-version:' "$work/dump" ;;
      svndumpfilter)
        make_svn || return 1
        /usr/bin/svnadmin dump "$repo" > "$work/dump" 2> "$work/prep" || return 1
        "$exe" include note.txt < "$work/dump" > "$work/filtered" 2> "$work/error" || return 1
        busy grep -Fq 'Node-path: note.txt' "$work/filtered" &&
        busy grep -Fq 'SVN-fs-dump-format-version:' "$work/filtered" ;;
      svnfsfs)
        make_svn || return 1
        "$exe" stats "$repo" > "$work/result" 2> "$work/error" || return 1
        busy grep -iqE 'revision|size|total' "$work/result" ;;
      svnlook)
        make_svn || return 1
        "$exe" cat "$repo" note.txt > "$work/note" 2> "$work/error" || return 1
        busy cmp -s "$work/note" /tests/m159-svn-note.txt &&
        "$exe" tree "$repo" > "$work/result" 2>&1 &&
        busy grep -Fq note.txt "$work/result" ;;
      svnmucc)
        make_svn || return 1
        "$exe" -m m159-mucc --non-interactive \
            put /tests/m159-svn-note.txt "$url/second.txt" \
            > "$work/result" 2> "$work/error" || return 1
        /usr/bin/svnlook cat "$repo" second.txt > "$work/note" 2>&1 || return 1
        busy cmp -s "$work/note" /tests/m159-svn-note.txt ;;
      svnrdump)
        make_svn || return 1
        "$exe" dump "$url" --non-interactive > "$work/dump" 2> "$work/error" || return 1
        busy grep -Fq 'Node-path: note.txt' "$work/dump" &&
        busy grep -Fq 'SVN-fs-dump-format-version:' "$work/dump" ;;
      svnversion)
        make_svn || return 1
        /usr/bin/svn checkout "$url" "$work/checkout" --non-interactive \
            > "$work/checkout.log" 2> "$work/checkout-error" || return 1
        "$exe" "$work/checkout" > "$work/result" 2> "$work/error" || return 1
        busy grep -Fxq 1 "$work/result" ;;

      certutil)
        make_nss || return 1
        "$exe" -L -d "sql:$db" -n m159-nss-fixture \
            > "$work/result" 2> "$work/error" || return 1
        busy grep -Fq 'NaSeed M159 ephemeral NSS fixture' "$work/result" ;;
      crlutil)
        make_nss || return 1
        "$exe" -L -d "sql:$db" > "$work/result" 2> "$work/error" || return 1
        busy grep -iqE 'crl|no.*list|empty' "$work/result" "$work/error" ;;
      modutil)
        make_nss || return 1
        "$exe" -list -dbdir "sql:$db" > "$work/result" 2> "$work/error" || return 1
        busy grep -Fq 'NSS Internal PKCS #11 Module' "$work/result" ;;
      pk12util)
        make_nss || return 1
        "$exe" -l /tests/m159-test-cert.p12 -W m159-fixture \
            > "$work/result" 2> "$work/error" || return 1
        busy grep -Fq 'm159-nss-fixture' "$work/result" "$work/error" ;;
      shlibsign)
        # Signs the authentic signed-APK NSS shared object with genuine
        # upstream integrity code; no generated host executable is involved.
        if ! "$exe" -i /usr/lib/libnss3.so -o "$work/libnss3.chk" \
            > "$work/result" 2> "$work/error"; then
            echo '[M159 SHLIBSIGN DIAG] original NSS integrity tool returned nonzero' >&2
            busy head -c 750 "$work/error" >&2 || :
            busy head -c 750 "$work/result" >&2 || :
            return 1
        fi
        if [ ! -s "$work/libnss3.chk" ]; then
            echo '[M159 SHLIBSIGN DIAG] NSS tool exited zero without a signature file' >&2
            return 1
        fi
        signed_bytes=$(busy wc -c < "$work/libnss3.chk")
        if [ "$signed_bytes" -lt 16 ]; then
            echo "[M159 SHLIBSIGN DIAG] signature too short: $signed_bytes bytes" >&2
            return 1
        fi ;;

      pamsplit)
        (cd "$work" && "$exe" /tests/m159-multi.pnm \
            > "$work/result" 2> "$work/error") || return 1
        image_check "$work/image0" "$work" &&
        image_check "$work/image1" "$work" ;;
      pamoil|pamshuffle|pamstereogram|pamfix|pamrecolor|pamhomography|pamsharpmap|pnmmercator)
        "$exe" "$src" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pamendian)
        # pamendian is a stdin-only filter and does not accept an input path.
        "$exe" < "$src" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pamtodjvurle)
        "$exe" "$bw" > "$work/out.djvu" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$work/out.djvu")" -gt 64 ] &&
        ! busy cmp -s "$bw" "$work/out.djvu" ;;
      pamsharpness|pamtilt)
        "$exe" "$src" > "$work/result" 2> "$work/error" || return 1
        busy grep -Eq '[0-9]+([.][0-9]+)?' "$work/result" ;;
      pamcrater)
        # pamcrater creates its own 16-bit elevation PAM (no filename args).
        "$exe" > "$out" 2> "$work/error" && image_check "$out" "$work" ;;
      pambackground)
        "$exe" "$src" > "$work/result" 2> "$work/error" || return 1
        [ -s "$work/result" ] && busy grep -qE '[0-9]' "$work/result" ;;
      pbmpage)
        "$exe" > "$out" 2> "$work/error" || return 1
        [ "$(busy wc -c < "$out")" -gt 1024 ] &&
        /usr/bin/pamfile "$out" > "$work/result" 2>&1 &&
        busy grep -Fq 'PBM' "$work/result" ;;
      pbmtext)
        printf 'NaSeed M159\n' | "$exe" > "$out" 2> "$work/error" || return 1
        image_check "$out" "$work" ;;
      *) echo "[M159 UNKNOWN ORIGINAL ELF] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in svn svnadmin svndumpfilter svnfsfs svnlook svnmucc svnrdump svnversion \
           certutil crlutil modutil pk12util shlibsign \
           pamsplit pamoil pamshuffle pamsharpness pamstereogram pamtodjvurle \
           pamfix pamcrater pamendian pamrecolor pambackground pamhomography \
           pamsharpmap pbmpage pbmtext pnmmercator pamtilt; do
    echo "[M159 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
    detail="/tmp/m159-$app-case.log"
    if /bin/sh /tests/m159-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M159_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M159 ORIGINAL FUNCTION FAIL] $app rc=$rc" >&2
        busy tail -35 "$detail" >&2 || :
        for stderr in /tmp/m159-$app/error /tmp/m159-$app/nss-import.log \
                      /tmp/m159-$app/import.log /tmp/m159-$app/create.log; do
            [ -s "$stderr" ] && busy head -c 560 "$stderr" >&2 || :
        done
        printf 'M159_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M159 BATCH FAIL] $failures/30 genuine Alpine operations" >&2
    echo M159_BATCH_FAIL
    exit 1
fi
echo M159_BATCH_PASS
