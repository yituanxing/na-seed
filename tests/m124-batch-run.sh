#!/bin/sh
# Fifteen independent original signed Alpine util-linux ELF executables.
# Host prepares images/source; only application semantics run inside NaSeed.
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
    echo "[M124 MISSING SIGNED ORIGINAL ELF] $*" >&2
    return 1
}

assert_minix_magic() {
    # V1 Minix with the default 30-byte filenames: magic 0x138F at +16.
    magic=$(busy dd if="$1" bs=1 skip=1040 count=2 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 8f13 ]
}

assert_cramfs_magic() {
    # Little-endian cramfs magic 0x28cd3d45.
    magic=$(busy dd if="$1" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 453dcd28 ]
}

check_input() {
    [ -s /tests/m124-empty-minix.img ] &&
    [ -s /tests/m124-empty-swap.img ] &&
    [ -s /tests/m124-cramfs-source/hello.txt ] &&
    [ -s /tests/m124-cramfs-source/nested/entry.txt ] &&
    [ -s /tests/m124-look-words.txt ]
}

run_case() {
    app=$1
    work="/tmp/m124-$app"
    busy mkdir -p "$work"
    case "$app" in
      getopt)
        exe=$(original_elf /bin/getopt)
        "$exe" -o a:b -l color: -- -a red --color=blue tail > "$work/out"
        busy grep -Fq -- "'red'" "$work/out"
        busy grep -Fq -- "--color 'blue'" "$work/out"
        busy grep -Fq -- "'tail'" "$work/out" ;;
      rev)
        exe=$(original_elf /bin/rev)
        busy printf 'abc123\n' | "$exe" > "$work/out"
        busy grep -Fxq '321cba' "$work/out" ;;
      cal)
        exe=$(original_elf /usr/bin/cal)
        "$exe" 2 2024 > "$work/out"
        busy grep -Fq 'February 2024' "$work/out"
        busy grep -Eq '(^|[[:space:]])29([[:space:]]|$)' "$work/out" ;;
      colrm)
        exe=$(original_elf /usr/bin/colrm)
        busy printf 'abcdefgh\n' | "$exe" 3 5 > "$work/out"
        busy grep -Fxq abfgh "$work/out" ;;
      column)
        exe=$(original_elf /usr/bin/column)
        busy printf 'first,second\none,two\n' |
          "$exe" -t -s , > "$work/out"
        busy grep -Eq '^one[[:space:]]+two$' "$work/out" ;;
      look)
        exe=$(original_elf /usr/bin/look)
        # Test original util-linux look with an immutable bootfs dictionary:
        # it mmap()s the file MAP_SHARED|PROT_READ to search sorted entries.
        "$exe" al /tests/m124-look-words.txt > "$work/out"
        busy printf 'alpha\nalpine\n' > "$work/expected"
        busy cmp -s "$work/expected" "$work/out" ;;
      namei)
        exe=$(original_elf /usr/bin/namei)
        busy mkdir -p "$work/dir"
        busy printf 'M124_NAMEI' > "$work/dir/leaf"
        "$exe" -l "$work/dir/leaf" > "$work/out"
        busy grep -Fq 'leaf' "$work/out"
        busy grep -Fq 'dir' "$work/out" ;;
      rename)
        exe=$(original_elf /usr/bin/rename)
        busy printf 'M124_RENAMED_CONTENT\n' > "$work/item.old"
        "$exe" .old .new "$work/item.old" > "$work/out" 2>&1
        [ -f "$work/item.new" ] && [ ! -e "$work/item.old" ]
        busy grep -Fxq M124_RENAMED_CONTENT "$work/item.new" ;;
      hardlink)
        exe=$(original_elf /usr/bin/hardlink)
        busy printf 'M124_IDENTICAL_DATA_0123456789\n' > "$work/a"
        busy cp "$work/a" "$work/b"
        before_a=$(busy stat -c %i "$work/a")
        before_b=$(busy stat -c %i "$work/b")
        [ "$before_a" != "$before_b" ]
        "$exe" -c -q "$work" > "$work/out" 2>&1
        after_a=$(busy stat -c %i "$work/a")
        after_b=$(busy stat -c %i "$work/b")
        [ "$after_a" = "$after_b" ]
        busy grep -Fxq M124_IDENTICAL_DATA_0123456789 "$work/a" ;;
      uuidparse)
        exe=$(original_elf /usr/bin/uuidparse)
        "$exe" 123e4567-e89b-42d3-a456-426614174000 > "$work/out"
        busy grep -Fiq '123e4567-e89b-42d3-a456-426614174000' "$work/out"
        busy grep -Fiq 'random' "$work/out" ;;
      mkfs.cramfs)
        exe=$(original_elf /sbin/mkfs.cramfs)
        "$exe" /tests/m124-cramfs-source "$work/fs.cramfs" > "$work/out" 2>&1
        [ -s "$work/fs.cramfs" ]
        assert_cramfs_magic "$work/fs.cramfs" ;;
      fsck.cramfs)
        exe=$(original_elf /sbin/fsck.cramfs)
        mkfs=$(original_elf /sbin/mkfs.cramfs)
        "$mkfs" /tests/m124-cramfs-source "$work/fs.cramfs" > "$work/make" 2>&1
        assert_cramfs_magic "$work/fs.cramfs"
        "$exe" --extract="$work/unpacked" "$work/fs.cramfs" > "$work/out" 2>&1
        busy cmp -s /tests/m124-cramfs-source/hello.txt "$work/unpacked/hello.txt"
        busy cmp -s /tests/m124-cramfs-source/nested/entry.txt "$work/unpacked/nested/entry.txt" ;;
      mkfs.minix)
        exe=$(original_elf /sbin/mkfs.minix)
        busy cp /tests/m124-empty-minix.img "$work/minix.img"
        "$exe" -1 "$work/minix.img" > "$work/out" 2>&1
        assert_minix_magic "$work/minix.img" ;;
      fsck.minix)
        exe=$(original_elf /sbin/fsck.minix)
        mkfs=$(original_elf /sbin/mkfs.minix)
        busy cp /tests/m124-empty-minix.img "$work/minix.img"
        "$mkfs" -1 "$work/minix.img" > "$work/make" 2>&1
        assert_minix_magic "$work/minix.img"
        "$exe" -f -s "$work/minix.img" > "$work/out" 2>&1
        busy grep -Ei 'inode|zone|file|Minix' "$work/out" > "$work/summary"
        [ -s "$work/summary" ] ;;
      mkswap)
        exe=$(original_elf /sbin/mkswap)
        busy cp /tests/m124-empty-swap.img "$work/swap.img"
        "$exe" -f "$work/swap.img" > "$work/out" 2>&1
        signature=$(busy dd if="$work/swap.img" bs=1 skip=4086 count=10 2>/dev/null)
        [ "$signature" = SWAPSPACE2 ] ;;
      *) echo "[M124 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M124_TRACE:-0}" = 1 ]; then
        PS4='[M124 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m124-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in getopt rev cal colrm column look namei rename hardlink uuidparse \
           mkfs.cramfs fsck.cramfs mkfs.minix fsck.minix mkswap; do
    echo "[M124 CASE START] $app"
    case_log="/tmp/m124-$(printf '%s' "$app" | busy tr '.+' '__')-case.log"
    if M124_TRACE=1 /bin/sh /tests/m124-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.+' '__')
        printf 'M124_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]' | busy tr '.+' '__')
        echo "[M124 $upper FAILED] signed native Linux ELF, rc=$rc" >&2
        busy tail -135 "$case_log" >&2
        case_dir="/tmp/m124-$app"
        for detail in out make summary header expected words; do
            if [ -s "$case_dir/$detail" ]; then
                printf '[M124 FAILED OUTPUT %s]\n' "$detail" >&2
                busy head -c 1600 "$case_dir/$detail" >&2
            fi
        done
        printf 'M124_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M124_BATCH_PASS
else
    echo "[M124 BATCH FAIL] $failed/15 original util-linux ELF programs" >&2
    echo M124_BATCH_FAIL
    exit 1
fi
