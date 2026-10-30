#!/bin/sh
# Fifteen genuinely distinct SIGNED Alpine ext2/FAT Linux ELF programs.
# Linux HOST preformats ext2 and creates blank disk images. No guest building.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
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
    echo "[M123 MISSING SIGNED ORIGINAL ELF] $*" >&2
    return 1
}

base=/tests/m123-ext2.img
blankext=/tests/m123-empty-ext.img
blankfat=/tests/m123-empty-fat.img

check_input() {
    [ -s "$base" ] && [ -s "$blankext" ] && [ -s "$blankfat" ]
    magic=$(busy dd if="$base" bs=1 skip=1080 count=2 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 53ef ]
}

assert_ext_header() {
    file=$1
    reader=$(original_elf /usr/sbin/dumpe2fs)
    "$reader" -h "$file" > "$work/header" 2>&1
    busy grep -Eq 'Filesystem magic number:[[:space:]]*0xEF53' "$work/header"
}

assert_ext_label() {
    assert_ext_header "$1"
    busy grep -Eq "Filesystem volume name:[[:space:]]*$2$" "$work/header"
}

assert_fat() {
    checker=$(original_elf /sbin/fsck.fat)
    "$checker" -n "$1" > "$work/fsck-fat" 2>&1
    busy grep -Fq 'files' "$work/fsck-fat"
}

run_case() {
    app=$1
    work="/tmp/m123-$app"
    busy mkdir -p "$work"
    case "$app" in
      mke2fs)
        exe=$(original_elf /sbin/mke2fs)
        busy cp "$blankext" "$work/new.ext2"
        "$exe" -q -F -t ext2 -b 4096 -L M123_CREATED "$work/new.ext2"
        assert_ext_label "$work/new.ext2" M123_CREATED ;;
      e2fsck)
        exe=$(original_elf /sbin/e2fsck)
        "$exe" -fn "$base" > "$work/check" 2>&1
        busy grep -Eq '[0-9]+/[0-9]+ files' "$work/check" ;;
      dumpe2fs)
        exe=$(original_elf /usr/sbin/dumpe2fs)
        "$exe" -h "$base" > "$work/dump" 2>&1
        busy grep -Eq 'Filesystem magic number:[[:space:]]*0xEF53' "$work/dump"
        busy grep -Eq 'Block count:[[:space:]]*2048$' "$work/dump" ;;
      debugfs)
        exe=$(original_elf /usr/sbin/debugfs)
        "$exe" -R 'stat /' "$base" > "$work/stat" 2>&1
        busy grep -Fq 'Inode: 2' "$work/stat"
        busy grep -Fq 'Type: directory' "$work/stat" ;;
      tune2fs)
        exe=$(original_elf /usr/sbin/tune2fs)
        busy cp "$base" "$work/tuned.img"
        "$exe" -L M123_TUNED "$work/tuned.img" > "$work/change" 2>&1
        assert_ext_label "$work/tuned.img" M123_TUNED ;;
      resize2fs)
        exe=$(original_elf /usr/sbin/resize2fs)
        busy cp "$base" "$work/shrink.img"
        "$exe" -f "$work/shrink.img" 6M > "$work/resize" 2>&1
        assert_ext_header "$work/shrink.img"
        busy grep -Eq 'Block count:[[:space:]]*1536$' "$work/header" ;;
      e2image)
        exe=$(original_elf /usr/sbin/e2image)
        "$exe" -r "$base" "$work/metadata.img" > "$work/export" 2>&1
        [ -s "$work/metadata.img" ]
        assert_ext_label "$work/metadata.img" M123_BASE ;;
      badblocks)
        exe=$(original_elf /usr/sbin/badblocks)
        "$exe" -b 4096 -c 16 -o "$work/badblocks" "$base" 16 0 > "$work/scan" 2>&1
        [ -f "$work/badblocks" ] && [ ! -s "$work/badblocks" ] ;;
      logsave)
        exe=$(original_elf /usr/sbin/logsave)
        "$exe" "$work/trace.log" /bin/busybox echo M123_LOGSAVE_TOKEN > "$work/console" 2>&1
        busy grep -Fq M123_LOGSAVE_TOKEN "$work/console"
        busy grep -Fq M123_LOGSAVE_TOKEN "$work/trace.log" ;;
      mklost+found)
        exe=$(original_elf /usr/sbin/mklost+found)
        busy mkdir -p "$work/new-root"
        ( cd "$work/new-root" && "$exe" > "$work/out" 2>&1 )
        [ -d "$work/new-root/lost+found" ]
        [ -r "$work/new-root/lost+found" ] ;;
      e2freefrag)
        exe=$(original_elf /usr/sbin/e2freefrag)
        "$exe" "$base" > "$work/fragments" 2>&1
        busy grep -Eq 'Blocksize:[[:space:]]*4096 bytes' "$work/fragments"
        busy grep -Eq 'Free blocks:[[:space:]]*[1-9][0-9]*' "$work/fragments" ;;
      e2undo)
        exe=$(original_elf /usr/sbin/e2undo)
        tune=$(original_elf /usr/sbin/tune2fs)
        busy cp "$base" "$work/undo.img"
        "$tune" -z "$work/change.undo" -L M123_CHANGED "$work/undo.img" > "$work/tune" 2>&1
        [ -s "$work/change.undo" ]
        assert_ext_label "$work/undo.img" M123_CHANGED
        "$exe" "$work/change.undo" "$work/undo.img" > "$work/revert" 2>&1
        assert_ext_label "$work/undo.img" M123_BASE ;;
      mkfs.fat)
        exe=$(original_elf /sbin/mkfs.fat)
        busy cp "$blankfat" "$work/fat.img"
        "$exe" -F 12 -n M123FAT "$work/fat.img" > "$work/format" 2>&1
        assert_fat "$work/fat.img" ;;
      fsck.fat)
        exe=$(original_elf /sbin/fsck.fat)
        mkfs=$(original_elf /sbin/mkfs.fat)
        busy cp "$blankfat" "$work/fat.img"
        "$mkfs" -F 12 -n M123FAT "$work/fat.img" > "$work/format" 2>&1
        "$exe" -n "$work/fat.img" > "$work/check" 2>&1
        busy grep -Fq 'files' "$work/check" ;;
      fatlabel)
        exe=$(original_elf /sbin/fatlabel)
        mkfs=$(original_elf /sbin/mkfs.fat)
        busy cp "$blankfat" "$work/fat.img"
        "$mkfs" -F 12 -n M123FAT "$work/fat.img" > "$work/format" 2>&1
        "$exe" "$work/fat.img" M123NEW > "$work/change" 2>&1
        "$exe" "$work/fat.img" > "$work/label"
        busy grep -Fxq M123NEW "$work/label"
        assert_fat "$work/fat.img" ;;
      *) echo "[M123 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M123_TRACE:-0}" = 1 ]; then
        PS4='[M123 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m123-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in mke2fs e2fsck dumpe2fs debugfs tune2fs resize2fs e2image badblocks \
           logsave mklost+found e2freefrag e2undo mkfs.fat fsck.fat fatlabel; do
    echo "[M123 CASE START] $app"
    case_log="/tmp/m123-$(printf '%s' "$app" | busy tr '.+' '__')-case.log"
    if M123_TRACE=1 /bin/sh /tests/m123-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.+' '__')
        printf 'M123_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.+' '__')
        echo "[M123 $upper FAILED] original signed Linux filesystem ELF, rc=$rc" >&2
        busy tail -135 "$case_log" >&2
        # Print every textual oracle failure in the same CI run.  Never dump
        # the binary ext2/FAT images or undo journals into the serial log.
        case_dir="/tmp/m123-$app"
        for detail in check format change resize export scan revert fragments dump stat header fsck-fat label out tune console trace.log; do
            if [ -s "$case_dir/$detail" ]; then
                printf '[M123 FAILED OUTPUT %s]\n' "$detail" >&2
                busy head -c 1600 "$case_dir/$detail" >&2
            fi
        done
        printf 'M123_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M123_BATCH_PASS
else
    echo "[M123 BATCH FAIL] $failed/15 original FS ELF applications" >&2
    echo M123_BATCH_FAIL
    exit 1
fi
