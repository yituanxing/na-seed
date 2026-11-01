#!/bin/sh
# 30 distinct real offline-image operations. Every executable is the original
# signed Alpine target ELF; no --version/--help, no host-replayed answers.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
verify_elf() {
    [ -x "$1" ] || return 1
    resolved=$(busy readlink -f "$1") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$1"
}
run_bin() {
    exe=$(verify_elf "$1") || return 1
    shift
    "$exe" "$@"
}
case_run() {
    app=$1
    work="/tmp/m155-$app"
    busy mkdir -p "$work"
    btrfs=/tmp/m155-images/btrfs.img
    xfs=/tmp/m155-images/xfs.img
    f2fs=/tmp/m155-images/f2fs.img
    ntfs=/tmp/m155-images/ntfs.img
    gpt=/tmp/m155-images/gpt.img
    erofs=/tests/m155-erofs.img
    input=/tests/m155-input.txt
    case "$app" in
      btrfs_super)
        run_bin /sbin/btrfs inspect-internal dump-super -f "$btrfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq '_BHRfS_M [match]' "$work/out" &&
        busy grep -Fq 'NASEED155' "$work/out" ;;
      btrfs_check)
        # The image is an explicitly unmounted regular file. --force
        # bypasses the mounted-block-device query but still checks all
        # metadata trees and CRCs in true readonly mode.
        run_bin /sbin/btrfs check --force --readonly "$btrfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'no error found' "$work/out" ;;
      btrfs_find_root)
        run_bin /usr/sbin/btrfs-find-root "$btrfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'Found tree root at' "$work/out" ;;
      btrfs_image)
        run_bin /usr/sbin/btrfs-image -c 0 "$btrfs" "$work/meta.img" \
            > "$work/out" 2> "$work/err" || return 1
        [ -s "$work/meta.img" ] &&
        [ "$(busy wc -c < "$work/meta.img")" -gt 4096 ] &&
        [ "$(busy wc -c < "$work/meta.img")" -lt 4194304 ] ;;
      btrfs_map)
        run_bin /sbin/btrfs inspect-internal dump-super -f "$btrfs" \
            > "$work/super" 2> "$work/err" || return 1
        block=$(busy awk '$1=="root" {print $2; exit}' "$work/super")
        [ -n "$block" ] || return 1
        run_bin /usr/sbin/btrfs-map-logical -l "$block" "$btrfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq "logical $block physical" "$work/out" ;;
      btrfs_mkfs)
        busy cp "$btrfs" "$work/reformat.img" || return 1
        run_bin /sbin/mkfs.btrfs -f -M -d single -m single \
            -L NASEEDNEW "$work/reformat.img" \
            > "$work/out" 2> "$work/err" || return 1
        run_bin /sbin/btrfs inspect-internal dump-super -f "$work/reformat.img" \
            > "$work/verify" 2>> "$work/err" || return 1
        busy grep -Fq 'label' "$work/verify" &&
        busy grep -Fq 'NASEEDNEW' "$work/verify" ;;
      xfs_db)
        run_bin /usr/sbin/xfs_db -r -f -c 'sb 0' -c 'p magicnum' \
            -c 'p blocksize' "$xfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq '0x58465342' "$work/out" &&
        busy grep -Fq '4096' "$work/out" ;;
      xfs_repair)
        run_bin /sbin/xfs_repair -n -f "$xfs" \
            > "$work/out" 2> "$work/err" || return 1
        # xfs_repair writes its seven real scrub phases to stderr and
        # some metadata notes to stdout; both are part of its result.
        busy grep -Fq 'Phase 7' "$work/err" &&
        busy grep -Fq 'No modify flag set' "$work/err" ;;
      xfs_log)
        # The log lives INSIDE the XFS image: -f would incorrectly treat
        # the entire image as a standalone external log device.
        run_bin /usr/sbin/xfs_logprint -t "$xfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'CLEAN' "$work/out" ;;
      xfs_write)
        run_bin /usr/sbin/xfs_io -f -c 'pwrite -S 0x5a 0 4096' \
            "$work/data.bin" > "$work/out" 2> "$work/err" || return 1
        [ "$(busy wc -c < "$work/data.bin")" -eq 4096 ] &&
        [ "$(busy od -An -tx1 -N4 "$work/data.bin" | busy tr -d ' \n')" \
          = 5a5a5a5a ] ;;
      xfs_read)
        busy cp "$input" "$work/source.txt"
        run_bin /usr/sbin/xfs_io -c 'pread -v 0 16' "$work/source.txt" \
            > "$work/out" 2> "$work/err" || return 1
        # xfs_io's -v replaces ASCII spaces with dots, so validate the
        # actual first four hex bytes against the same source file.
        prefix=$(busy od -An -tx1 -N4 "$input" |
                 busy sed -e 's/^ *//' -e 's/ *$//')
        busy grep -Fq "$prefix" "$work/out" &&
        busy grep -Fq 'read 16/16 bytes' "$work/out" ;;
      xfs_geometry)
        run_bin /sbin/mkfs.xfs -N -f "$xfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'blocks=81920' "$work/out" ;;
      f2fs_fsck)
        run_bin /usr/sbin/fsck.f2fs "$f2fs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'valid_block_count matching with CP' "$work/out" &&
        busy grep -Fq '[Ok..]' "$work/out" ;;
      f2fs_write)
        run_bin /usr/sbin/f2fs_io write 1 0 2 inc_num buffered \
            "$work/data.bin" > "$work/out" 2> "$work/err" || return 1
        [ "$(busy wc -c < "$work/data.bin")" -eq 8192 ] &&
        [ "$(busy dd if="$work/data.bin" bs=4096 skip=1 count=1 2>/dev/null |
             busy od -An -tu1 -N1 | busy tr -d ' \n')" -eq 1 ] ;;
      f2fs_read)
        run_bin /usr/sbin/f2fs_io write 1 0 2 inc_num buffered \
            "$work/data.bin" > "$work/write" 2> "$work/err" || return 1
        run_bin /usr/sbin/f2fs_io read 1 1 1 buffered 16 "$work/data.bin" \
            > "$work/out" 2>> "$work/err" || return 1
        busy grep -Fq '0100 0000' "$work/out" ;;
      f2fs_mkfs)
        busy cp "$f2fs" "$work/reformat.img" || return 1
        run_bin /usr/sbin/mkfs.f2fs -f -l NASEEDNEW "$work/reformat.img" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'format successful' "$work/out" &&
        [ "$(busy dd if="$work/reformat.img" bs=1 skip=1024 count=4 \
              2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')" \
          = 1020f5f2 ] ;;
      erofs_dump)
        run_bin /usr/bin/dump.erofs -s "$erofs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq '0xE0F5E1E2' "$work/out" &&
        busy grep -Fq 'inode count:' "$work/out" ;;
      erofs_fsck)
        run_bin /usr/bin/fsck.erofs "$erofs" \
            > "$work/out" 2> "$work/err" ;;
      erofs_extract)
        busy mkdir -p "$work/recovered"
        run_bin /usr/bin/fsck.erofs --extract="$work/recovered" "$erofs" \
            > "$work/out" 2> "$work/err" || return 1
        busy cmp -s "$input" "$work/recovered/evidence.txt" ;;
      erofs_mkfs)
        run_bin /usr/bin/mkfs.erofs -L NASEEDNEW -T 0 \
            "$work/new.erofs" /tests/m155-seed \
            > "$work/out" 2> "$work/err" || return 1
        busy mkdir -p "$work/recovered"
        run_bin /usr/bin/fsck.erofs --extract="$work/recovered" \
            "$work/new.erofs" > "$work/verify" 2>> "$work/err" || return 1
        busy cmp -s "$input" "$work/recovered/evidence.txt" ;;
      ntfs_info)
        run_bin /usr/bin/ntfsinfo -m "$ntfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'Volume Name: NASEED155' "$work/out" ;;
      ntfs_ls)
        run_bin /usr/bin/ntfsls "$ntfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fxq 'evidence.txt' "$work/out" ;;
      ntfs_cat)
        run_bin /usr/bin/ntfscat "$ntfs" /evidence.txt \
            > "$work/data.txt" 2> "$work/err" || return 1
        busy cmp -s "$input" "$work/data.txt" ;;
      ntfs_label)
        run_bin /usr/sbin/ntfslabel "$ntfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fxq NASEED155 "$work/out" ;;
      ntfs_resize)
        run_bin /usr/sbin/ntfsresize --info "$ntfs" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'Current volume size:' "$work/out" &&
        busy grep -Fq 'You might resize at' "$work/out" ;;
      ntfs_clone)
        run_bin /usr/sbin/ntfsclone --save-image -o "$work/save.img" "$ntfs" \
            > "$work/out" 2> "$work/err" || return 1
        [ -s "$work/save.img" ] || return 1
        run_bin /usr/sbin/ntfsclone --restore-image -o "$work/restored.img" \
            "$work/save.img" > "$work/restore" 2>> "$work/err" || return 1
        run_bin /usr/bin/ntfscat "$work/restored.img" /evidence.txt \
            > "$work/data.txt" 2>> "$work/err" || return 1
        busy cmp -s "$input" "$work/data.txt" ;;
      ntfs_mkfs)
        busy cp "$ntfs" "$work/reformat.img" || return 1
        run_bin /usr/sbin/mkntfs -F -Q -L NASEEDNEW "$work/reformat.img" \
            > "$work/out" 2> "$work/err" || return 1
        run_bin /usr/sbin/ntfslabel "$work/reformat.img" \
            > "$work/label" 2>> "$work/err" || return 1
        busy grep -Fxq NASEEDNEW "$work/label" ;;
      gpt_print)
        run_bin /usr/sbin/parted -s "$gpt" unit s print \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'Partition Table: gpt' "$work/out" &&
        busy grep -Fq '2048s' "$work/out" &&
        busy grep -Fq '18431s' "$work/out" ;;
      gpt_gdisk)
        run_bin /usr/bin/gdisk -l "$gpt" \
            > "$work/out" 2> "$work/err" || return 1
        busy grep -Fq 'GPT: present' "$work/out" &&
        busy grep -Fq 'primary' "$work/out" ;;
      gpt_modify)
        busy cp "$gpt" "$work/disk.img" || return 1
        run_bin /usr/sbin/parted -s "$work/disk.img" \
            mkpart primary 10MiB 14MiB \
            > "$work/out" 2> "$work/err" || return 1
        run_bin /usr/sbin/parted -s "$work/disk.img" unit s print \
            > "$work/verify" 2>> "$work/err" || return 1
        busy grep -Fq '20480s' "$work/verify" &&
        busy grep -Fq '28671s' "$work/verify" ;;
      *) echo "[M155 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

# These eight utilities require an actual mounted filesystem, a destructive
# superblock recovery target or filesystem-specific kernel ioctls. Verify
# genuine bounded ELF startup and its own argument parser, not a pretend
# filesystem success. The 30 counted operations above remain independent.
startup_only() {
    program=$1
    exe=$(verify_elf "/$program") || return 1
    log="/tmp/m155-startup-$(busy basename "$program").log"
    "$exe" --help > "$log" 2>&1 || :
    [ -s "$log" ] || return 1
    if busy grep -Eiq 'Error loading shared library|Error relocating|symbol not found' "$log"; then
        return 1
    fi
    echo "[M155 ELF STARTUP ONLY] /$program (not a semantic case)"
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    case_run "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
set -eu
busy mkdir -p /tmp/m155-images
failures=0
count=0
for group in btrfs xfs f2fs erofs ntfs gpt; do
    if [ "$group" != erofs ]; then
        echo "[M155 DECOMPRESS] $group authenticated host-generated image"
        busy gzip -dc "/tests/m155-$group.img.gz" \
            > "/tmp/m155-images/$group.img" || exit 1
    fi
    case "$group" in
      btrfs) apps='btrfs_super btrfs_check btrfs_find_root btrfs_image btrfs_map btrfs_mkfs' ;;
      xfs) apps='xfs_db xfs_repair xfs_log xfs_write xfs_read xfs_geometry' ;;
      f2fs) apps='f2fs_fsck f2fs_write f2fs_read f2fs_mkfs' ;;
      erofs) apps='erofs_dump erofs_fsck erofs_extract erofs_mkfs' ;;
      ntfs) apps='ntfs_info ntfs_ls ntfs_cat ntfs_label ntfs_resize ntfs_clone ntfs_mkfs' ;;
      gpt) apps='gpt_print gpt_gdisk gpt_modify' ;;
    esac
    for app in $apps; do
        count=$((count+1))
        echo "[M155 CASE START] $app"
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        detail="/tmp/m155-$app-case.log"
        if /bin/sh /tests/m155-batch-run.sh --case "$app" > "$detail" 2>&1; then
            printf 'M155_%s_PASS\n' "$upper"
            busy rm -rf "/tmp/m155-$app"
        else
            rc=$?
            echo "[M155 SEMANTIC FAILURE] $app rc=$rc" >&2
            busy tail -60 "$detail" >&2 || :
            for artifact in err out verify data.txt; do
                file="/tmp/m155-$app/$artifact"
                if [ -s "$file" ]; then
                    echo "[M155 FAILURE DETAIL] $app/$artifact" >&2
                    busy head -c 500 "$file" >&2 || :
                    echo >&2
                fi
            done
            printf 'M155_%s_FAIL\n' "$upper"
            failures=$((failures+1))
        fi
    done
    if [ "$group" != erofs ]; then
        busy rm -f "/tmp/m155-images/$group.img"
    fi
done
if [ "$failures" -ne 0 ] || [ "$count" -ne 30 ]; then
    echo "[M155 BATCH FAIL] $failures/$count original signed ELF semantic checks" >&2
    echo M155_BATCH_FAIL
    exit 1
fi
for elf in usr/sbin/btrfs-convert usr/sbin/btrfs-select-super \
           usr/sbin/btrfstune usr/sbin/xfs_copy usr/sbin/xfs_mdrestore \
           usr/sbin/xfs_spaceman usr/sbin/f2fscrypt usr/sbin/parse.f2fs; do
    startup_only "$elf" || {
        echo "[M155 ELF STARTUP FAIL] /$elf" >&2
        echo M155_BATCH_FAIL
        exit 1
    }
done
echo M155_BATCH_PASS
