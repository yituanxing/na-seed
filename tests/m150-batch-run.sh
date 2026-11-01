#!/bin/sh
# 26 independent, real input/output cases for signed unmodified Alpine ELFs.
# The ext2 and MBR fixtures are made on the HOST before the CPIO is packed;
# NaSeed executes the original binaries but does not build them or create FSs.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
image=/tests/m150-ext2.img
disk=/tests/m150-mbr.img
input=/tests/m150-input.txt
evidence=/tests/m150-seed/evidence.txt
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
evidence_inode() {
    # The inode comes from the ORIGINAL TSK directory parser, not an assumed
    # ext2 allocator layout or a fake fixed inode number.
    /usr/bin/fls "$image" | busy awk '/evidence.txt/ {
        gsub(":", "", $2); print $2; exit
    }'
}
file_nonempty() { [ -s "$work/$1" ]; }
case_run() {
    app=$1
    work="/tmp/m150-$app"
    busy mkdir -p "$work"
    case "$app" in
      lrzip)
        exe=$(original_elf /usr/bin/lrzip)
        "$exe" -q -o "$work/data.lrz" "$input" > "$work/output" 2> "$work/error" || return 1
        "$exe" -q -d -o "$work/roundtrip" "$work/data.lrz" >> "$work/output" 2>> "$work/error" || return 1
        busy cmp -s "$input" "$work/roundtrip" ;;
      pixz)
        exe=$(original_elf /usr/bin/pixz)
        "$exe" < "$input" > "$work/data.xz" 2> "$work/error" || return 1
        "$exe" -d < "$work/data.xz" > "$work/roundtrip" 2>> "$work/error" || return 1
        busy cmp -s "$input" "$work/roundtrip" ;;
      plzip)
        exe=$(original_elf /usr/bin/plzip)
        "$exe" -c "$input" > "$work/data.lz" 2> "$work/error" || return 1
        "$exe" -dc "$work/data.lz" > "$work/roundtrip" 2>> "$work/error" || return 1
        busy cmp -s "$input" "$work/roundtrip" ;;
      par2)
        exe=$(original_elf /usr/bin/par2)
        busy cp "$input" "$work/source.txt"
        "$exe" c "$work/data.par2" "$work/source.txt" > "$work/output" 2> "$work/error" || return 1
        file_nonempty data.par2 || return 1
        "$exe" v "$work/data.par2" >> "$work/output" 2>> "$work/error" || return 1
        busy grep -Eiq '(all files are correct|repair (will|is) not be required|recovery is not required)' "$work/output" ;;
      *)
        exe=$(original_elf "/usr/bin/$app")
        case "$app" in
          blkcalc)
            # Convert index zero in the real unallocated-block stream into
            # its corresponding on-disk ext2 allocation-unit address.
            "$exe" -u 0 "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Eq '[0-9]+' "$work/result" ;;
          img_cat)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy cmp -s "$image" "$work/result" ;;
          img_stat)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -iq 'raw' "$work/result" ;;
          tsk_imageinfo)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            file_nonempty result ;;
          tsk_gettimes)
            # A direct ext2 image avoids depending on the tool's optional
            # volume-table auto-scan when collecting real MACtime body rows.
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'evidence.txt' "$work/result" ;;
          fsstat)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'FILE SYSTEM INFORMATION' "$work/result" ;;
          fls)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'evidence.txt' "$work/result" ;;
          icat|istat|ffind|ifind|fcat)
            inode=$(evidence_inode)
            [ -n "$inode" ] || return 1
            case "$app" in
              icat)
                "$exe" "$image" "$inode" > "$work/result" 2> "$work/error" || return 1
                busy cmp -s "$evidence" "$work/result" ;;
              istat)
                "$exe" "$image" "$inode" > "$work/result" 2> "$work/error" || return 1
                busy grep -Fq "$inode" "$work/result" ;;
              ffind)
                "$exe" "$image" "$inode" > "$work/result" 2> "$work/error" || return 1
                busy grep -Fq 'evidence.txt' "$work/result" ;;
              ifind)
                "$exe" -n /evidence.txt "$image" > "$work/result" 2> "$work/error" || return 1
                busy grep -Fq "$inode" "$work/result" ;;
              fcat)
                "$exe" /evidence.txt "$image" > "$work/result" 2> "$work/error" || return 1
                busy cmp -s "$evidence" "$work/result" ;;
            esac ;;
          blkcat)
            "$exe" "$image" 2 > "$work/result" 2> "$work/error" || return 1
            file_nonempty result ;;
          blkstat)
            "$exe" "$image" 2 > "$work/result" 2> "$work/error" || return 1
            busy grep -Eiq '(block|sector)' "$work/result" ;;
          blkls)
            # Block 2 may be metadata/allocated. -e copies every block;
            # the default unallocated-only mode would silently emit zero.
            "$exe" -e "$image" 2 > "$work/result" 2> "$work/error" || return 1
            file_nonempty result ;;
          ils)
            "$exe" -a "$image" > "$work/result" 2> "$work/error" || return 1
            file_nonempty result ;;
          fiwalk)
            "$exe" -X"$work/result.xml" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'evidence.txt' "$work/result.xml" ;;
          tsk_recover)
            busy mkdir -p "$work/recovered"
            "$exe" -a "$image" "$work/recovered" > "$work/result" 2> "$work/error" || return 1
            busy cmp -s "$evidence" "$work/recovered/evidence.txt" ;;
          srch_strings)
            "$exe" "$image" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'NASEED_M150_EVIDENCE' "$work/result" ;;
          mmls)
            "$exe" "$disk" > "$work/result" 2> "$work/error" || return 1
            busy grep -Fq 'Linux (0x83)' "$work/result" ;;
          mmstat)
            "$exe" "$disk" > "$work/result" 2> "$work/error" || return 1
            busy grep -iq 'dos' "$work/result" ;;
          mmcat)
            "$exe" "$disk" 2 > "$work/result" 2> "$work/error" || return 1
            busy cmp -s "$image" "$work/result" ;;
          *) echo "[M150 UNKNOWN ORIGINAL ELF] $app" >&2; return 2 ;;
        esac ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failures=0
for app in lrzip pixz plzip par2 blkcalc blkcat blkls blkstat fcat \
           ffind fiwalk fls fsstat icat ifind ils img_cat img_stat istat \
           mmcat mmls mmstat srch_strings tsk_imageinfo tsk_gettimes tsk_recover; do
    echo "[M150 CASE START] $app"
    upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
    detail="/tmp/m150-$app-case.log"
    if /bin/sh /tests/m150-batch-run.sh --case "$app" > "$detail" 2>&1; then
        printf 'M150_%s_PASS\n' "$upper"
    else
        rc=$?
        echo "[M150 ORIGINAL ELF FAILED] $app rc=$rc" >&2
        busy tail -100 "$detail" >&2 || :
        for artifact in error result output result.xml roundtrip; do
            if [ -s "/tmp/m150-$app/$artifact" ]; then
                echo "[M150 FAILURE DETAIL] $app/$artifact" >&2
                busy head -c 400 "/tmp/m150-$app/$artifact" >&2 || :
                echo >&2
            fi
        done
        printf 'M150_%s_FAIL\n' "$upper"
        failures=$((failures+1))
    fi
done
if [ "$failures" -ne 0 ]; then
    echo "[M150 BATCH FAIL] $failures/26 original signed Alpine semantic checks" >&2
    echo M150_BATCH_FAIL
    exit 1
fi
echo M150_BATCH_PASS
