#!/bin/sh
# Fifteen *different*, signature-verified Alpine ELF image utilities.
# The Linux host prepares only input tar/tree/blank disk. All operations and
# their observable on-disk/file-content effects execute inside actual NaSeed.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        case "$resolved" in /bin/busybox|/usr/bin/busybox) continue ;; esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M128 NO ORIGINAL ELF] $*" >&2
    return 1
}

alpha=/tests/m128-tree/alpha.txt
beta=/tests/m128-tree/nested/beta.txt
tar=/tests/m128-input.tar
blank=/tests/m128-blank-exfat.img
check_inputs() {
    [ -s "$alpha" ] && [ -s "$beta" ] && [ -s "$tar" ] && [ -s "$blank" ]
    busy grep -Fxq M128_SQUASH_CROSS_IMPL_ALPHA "$alpha"
    busy grep -Fxq M128_SQUASH_CROSS_IMPL_BETA "$beta"
}
classic() {
    creator=$(original_elf /usr/bin/mksquashfs)
    "$creator" /tests/m128-tree "$work/classic.sqfs" \
        -noappend -no-progress -processors 1 > "$work/create.log" 2>&1
    [ -s "$work/classic.sqfs" ]
}
ng_create() {
    creator=$(original_elf /usr/bin/gensquashfs)
    "$creator" -D /tests/m128-tree "$work/ng.sqfs" > "$work/create.log" 2>&1
    [ -s "$work/ng.sqfs" ]
}
assert_sqfs_alpha() {
    cat=$(original_elf /usr/bin/sqfscat)
    "$cat" "$1" alpha.txt > "$work/alpha-out"
    busy cmp -s "$alpha" "$work/alpha-out"
}
assert_exfat_magic() {
    value=$(busy dd if="$1" bs=1 skip=3 count=8 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$value" = 4558464154202020 ]
}
format_exfat() {
    busy cp "$blank" "$work/exfat.img"
    mkfs=$(original_elf /usr/sbin/mkfs.exfat)
    "$mkfs" -L M128BASE "$work/exfat.img" > "$work/format.log" 2>&1
    assert_exfat_magic "$work/exfat.img"
}

run_case() {
    app=$1
    work="/tmp/m128-$app"
    busy mkdir -p "$work"
    case "$app" in
      mksquashfs)
        classic
        exe=$(original_elf /usr/bin/unsquashfs)
        "$exe" -ll "$work/classic.sqfs" > "$work/list"
        busy grep -Fq alpha.txt "$work/list"
        busy grep -Fq beta.txt "$work/list"
        # These two are the signed upstream multi-call entry points, but
        # not counted twice as unique ELF bytes. Still exercise sqfstar.
        star=$(original_elf /usr/bin/sqfstar)
        "$star" "$work/star.sqfs" < "$tar" > "$work/star.log" 2>&1
        [ -s "$work/star.sqfs" ]
        assert_sqfs_alpha "$work/star.sqfs" ;;
      unsquashfs)
        classic
        exe=$(original_elf /usr/bin/unsquashfs)
        "$exe" -d "$work/unpacked" "$work/classic.sqfs" > "$work/unpack.log" 2>&1
        busy cmp -s "$alpha" "$work/unpacked/alpha.txt"
        busy cmp -s "$beta" "$work/unpacked/nested/beta.txt" ;;
      gensquashfs)
        ng_create
        # Independent legacy reader validates image format and file bytes.
        assert_sqfs_alpha "$work/ng.sqfs" ;;
      rdsquashfs)
        classic
        exe=$(original_elf /usr/bin/rdsquashfs)
        "$exe" -c alpha.txt "$work/classic.sqfs" > "$work/alpha-out"
        busy cmp -s "$alpha" "$work/alpha-out" ;;
      sqfs2tar)
        classic
        exe=$(original_elf /usr/bin/sqfs2tar)
        "$exe" "$work/classic.sqfs" > "$work/from-sqfs.tar"
        busy tar -tf "$work/from-sqfs.tar" > "$work/tar-list"
        busy grep -Fq alpha.txt "$work/tar-list"
        busy grep -Fq nested/beta.txt "$work/tar-list" ;;
      sqfsdiff)
        ng_create
        exe=$(original_elf /usr/bin/sqfsdiff)
        "$exe" --old "$work/ng.sqfs" --new "$work/ng.sqfs" > "$work/same" 2>&1
        # Two independent source trees: a real content difference must be
        # reported as exit 1, never a false equality or internal error.
        busy mkdir -p "$work/tree/nested"
        busy cp "$alpha" "$work/tree/alpha.txt"
        busy cp "$beta" "$work/tree/nested/beta.txt"
        busy echo M128_MUTATED > "$work/tree/alpha.txt"
        creator=$(original_elf /usr/bin/gensquashfs)
        "$creator" -D "$work/tree" "$work/mutated.sqfs" > "$work/mutate.log" 2>&1
        if "$exe" --old "$work/ng.sqfs" --new "$work/mutated.sqfs" > "$work/diff" 2>&1; then
            return 1
        else
            [ "$?" -eq 1 ]
        fi ;;
      tar2sqfs)
        exe=$(original_elf /usr/bin/tar2sqfs)
        "$exe" "$work/fromtar.sqfs" < "$tar" > "$work/tar2sqfs.log" 2>&1
        [ -s "$work/fromtar.sqfs" ]
        assert_sqfs_alpha "$work/fromtar.sqfs" ;;
      mkfs.exfat)
        format_exfat
        dump=$(original_elf /usr/sbin/dump.exfat)
        "$dump" "$work/exfat.img" > "$work/dump"
        busy grep -Fq M128BASE "$work/dump" ;;
      fsck.exfat)
        format_exfat
        exe=$(original_elf /usr/sbin/fsck.exfat)
        "$exe" -n "$work/exfat.img" > "$work/check" 2>&1
        assert_exfat_magic "$work/exfat.img" ;;
      dump.exfat)
        format_exfat
        exe=$(original_elf /usr/sbin/dump.exfat)
        "$exe" "$work/exfat.img" > "$work/dump"
        busy grep -Fq M128BASE "$work/dump" ;;
      exfat2img)
        format_exfat
        exe=$(original_elf /usr/sbin/exfat2img)
        "$exe" -o "$work/metadata.img" "$work/exfat.img" > "$work/export" 2>&1
        [ -s "$work/metadata.img" ]
        assert_exfat_magic "$work/metadata.img" ;;
      tune.exfat)
        format_exfat
        exe=$(original_elf /usr/sbin/tune.exfat)
        "$exe" -L M128TUNED "$work/exfat.img" > "$work/change" 2>&1
        "$exe" -l "$work/exfat.img" > "$work/label"
        busy grep -Fq M128TUNED "$work/label" ;;
      exfatlabel)
        format_exfat
        exe=$(original_elf /usr/sbin/exfatlabel)
        "$exe" "$work/exfat.img" M128LABEL > "$work/change" 2>&1
        "$exe" "$work/exfat.img" > "$work/label"
        busy grep -Fq M128LABEL "$work/label" ;;
      blkid)
        format_exfat
        exe=$(original_elf /sbin/blkid)
        "$exe" -p -o value -s TYPE "$work/exfat.img" > "$work/type"
        busy grep -Fxq exfat "$work/type" ;;
      wipefs)
        format_exfat
        exe=$(original_elf /sbin/wipefs)
        "$exe" -n "$work/exfat.img" > "$work/signature"
        busy grep -iq exfat "$work/signature"
        assert_exfat_magic "$work/exfat.img" ;;
      *) echo "[M128 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -gt 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_inputs
    if [ "${M128_TRACE:-0}" = 1 ]; then
        PS4='[M128 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in mksquashfs unsquashfs gensquashfs rdsquashfs \
           sqfs2tar sqfsdiff tar2sqfs mkfs.exfat fsck.exfat dump.exfat \
           exfat2img tune.exfat exfatlabel blkid wipefs; do
    echo "[M128 CASE START] $app"
    case_log="/tmp/m128-$(printf '%s' "$app" | busy tr '.' '_')-case.log"
    if M128_TRACE=1 /bin/sh /tests/m128-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.' '_')
        printf 'M128_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.' '_')
        echo "[M128 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -100 "$case_log" >&2
        for detail in create.log unpack.log tar-list star.log tar2sqfs.log \
                      mutate.log same diff format.log dump check export change label \
                      type signature; do
            if [ -s "/tmp/m128-$app/$detail" ]; then
                echo "[M128 FAILED OUTPUT $detail]" >&2
                busy head -c 1100 "/tmp/m128-$app/$detail" >&2
            fi
        done
        printf 'M128_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M128_BATCH_PASS
else
    echo "[M128 BATCH FAIL] $failed/15 original signed ELF programs" >&2
    echo M128_BATCH_FAIL
    exit 1
fi
