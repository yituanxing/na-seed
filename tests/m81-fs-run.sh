#!/bin/sh
# M81: run the SAME unmodified original Alpine M80 program oracles on three
# genuine, independently mounted writable kernel VFS backends in one boot.
# Do not replace the existing M80 tests, installer, shell, or QEMU runner.
# A path's mere existence proves nothing: /proc/mounts is the live VFS registry.

PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
export PATH HOME LC_ALL LANG TERM

mount_is_writable() {
    mount_path=$1
    mount_type=$2
    [ -d "$mount_path" ] || return 1
    [ -r /proc/mounts ] || return 1
    while read -r source target type mode trailing; do
        [ "$target" = "$mount_path" ] || continue
        if [ "$type" = "$mount_type" ] && [ "$mode" = rw ]; then
            return 0
        fi
        echo "[M81 WRONG MOUNT] $mount_path actual=$type/$mode expected=$mount_type/rw" >&2
        return 1
    done < /proc/mounts
    echo "[M81 MOUNT MISSING] $mount_path expected=$mount_type/rw" >&2
    return 1
}

cross_case() {
    kind=$1
    case "$kind" in
      rsync)
        # Original rsync traverses real mount boundaries in both directions.
        /bin/busybox mkdir -p /mnt/m81-cross-rsync /ramfs/m81-cross-rsync
        /usr/bin/rsync -r /tmp/m80-grep/data.txt /mnt/m81-cross-rsync/
        /usr/bin/rsync -r /mnt/m81-cross-rsync/data.txt /ramfs/m81-cross-rsync/
        /bin/busybox cmp /tmp/m80-grep/data.txt /mnt/m81-cross-rsync/data.txt
        /bin/busybox cmp /tmp/m80-grep/data.txt /ramfs/m81-cross-rsync/data.txt ;;
      tar)
        # Original GNU tar archives a third-party RamFS tree to ext4 and
        # extracts into tmpfs. Real fchown and other VFS metadata apply.
        /bin/tar -cf /mnt/m81-cross-ramfs.tar -C /ramfs m80-grep
        /bin/busybox mkdir -p /tmp/m81-cross-tar
        /bin/tar -xf /mnt/m81-cross-ramfs.tar -C /tmp/m81-cross-tar
        /bin/busybox cmp /ramfs/m80-grep/data.txt /tmp/m81-cross-tar/m80-grep/data.txt ;;
      zstd)
        # Real compression writes ext4, then decompression writes RamFS.
        /usr/bin/zstd -q -c /tmp/m80-grep/data.txt > /mnt/m81-cross-data.zst
        /usr/bin/zstd -q -dc /mnt/m81-cross-data.zst > /ramfs/m81-cross-data.txt
        /bin/busybox cmp /tmp/m80-grep/data.txt /ramfs/m81-cross-data.txt ;;
      sqlite)
        # Original SQLite creates a database on ext4; original rsync moves
        # its bytes to RamFS, where a second SQLite process reads them.
        /usr/bin/sqlite3 /mnt/m81-cross-data.db \
          "CREATE TABLE m81(k TEXT PRIMARY KEY,v INTEGER); INSERT INTO m81 VALUES('cross',81);"
        /usr/bin/rsync -r /mnt/m81-cross-data.db /ramfs/m81-cross-data.db
        /usr/bin/sqlite3 /ramfs/m81-cross-data.db \
          "SELECT k || ':' || v FROM m81;" > /tmp/m81-cross-sqlite-actual
        printf 'cross:81\n' > /tmp/m81-cross-sqlite-expected
        /bin/busybox cmp /tmp/m81-cross-sqlite-actual /tmp/m81-cross-sqlite-expected ;;
      persistence)
        # Host must additionally verify this EXACT file on the raw VirtIO
        # ext4 image with debugfs and verify clean filesystem with e2fsck.
        printf 'NaSeed-M81-real-ext4-persistence\n' > /mnt/m81-fs-persisted.txt
        /bin/busybox sync
        [ "$(/bin/busybox cat /mnt/m81-fs-persisted.txt)" = \
          'NaSeed-M81-real-ext4-persistence' ] ;;
      *) echo "[M81 UNKNOWN CROSS CASE] $kind" >&2; return 2 ;;
    esac
}

if [ "${1:-}" = --cross ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    cross_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m81-fs-run.sh [--cross case]' >&2; exit 2; }

# No provider substitutes, e.g. a plain /mnt directory in bootfs, are allowed.
mount_failures=0
if mount_is_writable /tmp tmpfs; then
    echo M81_MOUNT_TMPFS_PASS
else
    echo M81_MOUNT_TMPFS_FAIL
    mount_failures=$((mount_failures + 1))
fi
if mount_is_writable /ramfs upstream-ramfs; then
    echo M81_MOUNT_RAMFS_PASS
else
    echo M81_MOUNT_RAMFS_FAIL
    mount_failures=$((mount_failures + 1))
fi
if mount_is_writable /mnt lwext4; then
    echo M81_MOUNT_EXT4_PASS
else
    echo M81_MOUNT_EXT4_FAIL
    mount_failures=$((mount_failures + 1))
fi
if [ "$mount_failures" -ne 0 ]; then
    echo "[M81 MOUNT FAIL] $mount_failures of 3 real writable VFS providers unavailable" >&2
    echo M81_FS_BATCH_FAIL
    exit 1
fi

failures=0
cases=0
for provider in TMPFS RAMFS EXT4; do
    case "$provider" in
      TMPFS) root=/tmp ;;
      RAMFS) root=/ramfs ;;
      EXT4) root=/mnt ;;
    esac
    provider_failures=0
    for app in grep sed gawk find coreutils jq ripgrep fd tree \
               tar gzip xz bzip2 zip unzip zstd sqlite bc rsync; do
        label=$(printf '%s' "$app" | /bin/busybox tr '[:lower:]' '[:upper:]')
        cases=$((cases + 1))
        if M80_WORK_ROOT="$root" /bin/sh /tests/m80-batch-run.sh --case "$app"; then
            echo "M81_${provider}_${label}_PASS"
        else
            status=$?
            echo "[M81 ${provider}/${label} FAILED] original signed ELF filesystem behavior, rc=$status" >&2
            echo "M81_${provider}_${label}_FAIL"
            failures=$((failures + 1))
            provider_failures=$((provider_failures + 1))
        fi
    done
    if [ "$provider_failures" -eq 0 ]; then
        echo "M81_${provider}_ALL19_PASS"
    else
        echo "M81_${provider}_ALL19_FAIL"
    fi
done

# Independent mixed-provider workloads; failures never conceal later cases.
for cross in rsync tar zstd sqlite persistence; do
    if /bin/sh /tests/m81-fs-run.sh --cross "$cross"; then
        case "$cross" in
          persistence) echo M81_EXT4_PERSISTENCE_PASS ;;
          *) label=$(printf '%s' "$cross" | /bin/busybox tr '[:lower:]' '[:upper:]')
             echo "M81_CROSS_${label}_PASS" ;;
        esac
    else
        status=$?
        echo "[M81 CROSS/$cross FAILED] original programs and VFS, rc=$status" >&2
        case "$cross" in
          persistence) echo M81_EXT4_PERSISTENCE_FAIL ;;
          *) label=$(printf '%s' "$cross" | /bin/busybox tr '[:lower:]' '[:upper:]')
             echo "M81_CROSS_${label}_FAIL" ;;
        esac
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ] || [ "$cases" -ne 57 ]; then
    echo "[M81 FS BATCH FAIL] $failures failures among 57 provider-program plus 5 cross-provider cases (observed=$cases)" >&2
    echo M81_FS_BATCH_FAIL
    exit 1
fi
echo M81_FS_BATCH_PASS

# Extend the SAME actual QEMU boot, mounted providers and signed original
# Alpine rootfs into M82. No second filesystem/installer/runner is involved.
# An M82 failure makes this guest command fail despite a successful M81 gate.
/bin/sh /tests/m82-make-run.sh
