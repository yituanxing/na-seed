#!/bin/sh
# M80: actual signed Alpine binaries process real inputs inside NaSeed.
# Keep every application isolated: one failure must NOT hide other failures.
# `--case` runs in a fresh shell with errexit; the outer runner records all
# 19 individual statuses before it decides whether the batch passed.

PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
export PATH HOME LC_ALL LANG TERM

original_elf() {
    # Paths are the signed Alpine v3.23 APK ELF manifest, not BusyBox applets.
    [ "$#" -eq 1 ] || return 1
    path=$1
    [ -x "$path" ] || { echo "[M80 MISSING ELF] $path" >&2; return 1; }
    resolved=$(/bin/busybox readlink -f "$path") || return 1
    [ -x "$resolved" ] || return 1
    case "$resolved" in
      /bin/busybox|/usr/bin/busybox)
        echo "[M80 NOT ORIGINAL] $path -> BusyBox" >&2
        return 1 ;;
    esac
    magic=$(/bin/busybox dd if="$resolved" bs=1 count=4 2>/dev/null |
           /bin/busybox od -An -tx1 | /bin/busybox tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
      echo "[M80 NOT ELF] $path -> $resolved" >&2
      return 1
    }
}

case_run() {
    app=$1
    # M81 runs the SAME signed-program oracles against independent VFS
    # providers. The M80 default still targets the original tmpfs paths.
    work=${M80_WORK_ROOT:-/tmp}/m80-"$app"
    /bin/busybox mkdir -p "$work"
    printf 'alpha one\nbeta two\ngamma three\n' > "$work/data.txt"

    case "$app" in
      grep)
        original_elf /bin/grep
        /bin/grep -Fx 'beta two' "$work/data.txt" > "$work/actual"
        printf 'beta two\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      sed)
        original_elf /bin/sed
        /bin/sed 's/beta two/beta TWO/' "$work/data.txt" > "$work/actual"
        printf 'alpha one\nbeta TWO\ngamma three\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      gawk)
        original_elf /usr/bin/gawk
        /usr/bin/gawk '$1=="gamma" {print $2}' "$work/data.txt" > "$work/actual"
        printf 'three\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      find)
        original_elf /usr/bin/find
        /usr/bin/find "$work" -type f -name data.txt -print > "$work/actual"
        printf '%s\n' "$work/data.txt" > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      coreutils)
        # sha256sum is an Alpine symlink to genuine /bin/coreutils, not BusyBox.
        original_elf /usr/bin/sha256sum
        /bin/busybox cp "$work/data.txt" "$work/data-copy.txt"
        /usr/bin/sha256sum "$work/data.txt" > "$work/hash1"
        /usr/bin/sha256sum "$work/data-copy.txt" > "$work/hash2"
        a=$(/bin/busybox cut -d ' ' -f 1 "$work/hash1")
        b=$(/bin/busybox cut -d ' ' -f 1 "$work/hash2")
        [ -n "$a" ] && [ "$a" = "$b" ] ;;
      jq)
        original_elf /usr/bin/jq
        printf '{"name":"alpha","count":3}\n' > "$work/input.json"
        /usr/bin/jq -c '.count += 2 | [.name,.count]' "$work/input.json" > "$work/actual"
        printf '["alpha",5]\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      ripgrep)
        original_elf /usr/bin/rg
        /usr/bin/rg --no-heading --color never -Fx 'beta two' "$work/data.txt" > "$work/actual"
        printf 'beta two\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      fd)
        original_elf /usr/bin/fd
        /usr/bin/fd -a -t f -g 'data.txt' "$work" > "$work/actual"
        printf '%s\n' "$work/data.txt" > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      tree)
        original_elf /usr/bin/tree
        /usr/bin/tree -afi "$work" > "$work/actual"
        /bin/busybox grep -Fxq "$work/data.txt" "$work/actual" ;;
      tar)
        original_elf /bin/tar
        /bin/busybox mkdir -p "$work/out"
        /bin/tar -cf "$work/archive.tar" -C "$work" data.txt
        /bin/tar -xf "$work/archive.tar" -C "$work/out"
        /bin/busybox cmp "$work/data.txt" "$work/out/data.txt" ;;
      gzip)
        original_elf /bin/gzip
        /bin/gzip -c "$work/data.txt" > "$work/data.gz"
        /bin/gzip -dc "$work/data.gz" > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      xz)
        original_elf /usr/bin/xz
        /usr/bin/xz -c "$work/data.txt" > "$work/data.xz"
        /usr/bin/xz -dc "$work/data.xz" > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      bzip2)
        original_elf /usr/bin/bzip2
        /usr/bin/bzip2 -c "$work/data.txt" > "$work/data.bz2"
        /usr/bin/bzip2 -dc "$work/data.bz2" > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      zip)
        original_elf /usr/bin/zip
        (cd "$work" && /usr/bin/zip -q archive.zip data.txt)
        [ -s "$work/archive.zip" ]
        # Keep this independent from the separately tested original `unzip`:
        # the Alpine BusyBox unzip applet is merely the ZIP-format oracle.
        /bin/busybox unzip -p "$work/archive.zip" data.txt > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      unzip)
        original_elf /usr/bin/unzip
        # Known independent ZIP fixture, constructed on host Linux from exactly
        # the deterministic data.txt bytes. BusyBox has no portable zip applet.
        printf '%s' 'UEsDBBQAAAAIAO2EXl2S7jwTHwAAAB8AAAAIAAAAZGF0YS50eHRLzCnISFTIz0vlSkotSVQoKc/nSk/MzQWyMopSU7kAUEsBAhQDFAAAAAgA7YReXZLuPBMfAAAAHwAAAAgAAAAAAAAAAAAAAIABAAAAAGRhdGEudHh0UEsFBgAAAAABAAEANgAAAEUAAAAAAA==' |
          /bin/busybox base64 -d > "$work/input.zip"
        /usr/bin/unzip -p "$work/input.zip" data.txt > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      zstd)
        original_elf /usr/bin/zstd
        /usr/bin/zstd -q -c "$work/data.txt" > "$work/data.zst"
        /usr/bin/zstd -q -dc "$work/data.zst" > "$work/actual"
        /bin/busybox cmp "$work/data.txt" "$work/actual" ;;
      sqlite)
        original_elf /usr/bin/sqlite3
        /usr/bin/sqlite3 "$work/data.db" \
          "CREATE TABLE t(k TEXT PRIMARY KEY,v INTEGER); BEGIN; INSERT INTO t VALUES('alpha',11),('beta',22); COMMIT;"
        /usr/bin/sqlite3 "$work/data.db" \
          "SELECT k || ':' || v FROM t ORDER BY k;" > "$work/actual"
        printf 'alpha:11\nbeta:22\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      bc)
        original_elf /usr/bin/bc
        printf '7*6\n' | /usr/bin/bc > "$work/actual"
        printf '42\n' > "$work/expected"
        /bin/busybox cmp "$work/actual" "$work/expected" ;;
      rsync)
        original_elf /usr/bin/rsync
        /bin/busybox mkdir -p "$work/src" "$work/dst"
        /bin/busybox cp "$work/data.txt" "$work/src/data.txt"
        /bin/busybox chmod 640 "$work/src/data.txt"
        /usr/bin/rsync -a "$work/src/" "$work/dst/"
        /bin/busybox cmp "$work/src/data.txt" "$work/dst/data.txt"
        [ "$(/bin/busybox stat -c %a "$work/src/data.txt")" = 640 ]
        [ "$(/bin/busybox stat -c %a "$work/dst/data.txt")" = 640 ]
        [ -f "$work/dst/data.txt" ] ;;
      *) echo "[M80 UNKNOWN APP] $app" >&2; return 2 ;;
    esac
}

if [ "${1:-}" = '--case' ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    case_run "$2"
    exit 0
fi

[ "$#" -eq 0 ] || { echo 'usage: m80-batch-run.sh [--case app]' >&2; exit 2; }
failures=0
text_fail=0
archive_fail=0
data_fail=0
for app in grep sed gawk find coreutils jq ripgrep fd tree \
           tar gzip xz bzip2 zip unzip zstd sqlite bc rsync; do
    if /bin/sh /tests/m80-batch-run.sh --case "$app"; then
        label=$(printf '%s' "$app" | /bin/busybox tr '[:lower:]' '[:upper:]')
        echo "M80_${label}_PASS"
    else
        rc=$?
        label=$(printf '%s' "$app" | /bin/busybox tr '[:lower:]' '[:upper:]')
        echo "[M80 ${label} FAILED] original ELF or functional oracle, rc=$rc" >&2
        echo "M80_${label}_FAIL"
        failures=$((failures + 1))
        case "$app" in
          grep|sed|gawk|find|coreutils|jq|ripgrep|fd|tree) text_fail=$((text_fail + 1)) ;;
          tar|gzip|xz|bzip2|zip|unzip|zstd) archive_fail=$((archive_fail + 1)) ;;
          sqlite|bc) data_fail=$((data_fail + 1)) ;;
        esac
    fi
done

if [ "$text_fail" -eq 0 ]; then echo M80_TEXT_TREE_PASS; else echo M80_TEXT_TREE_FAIL; fi
if [ "$archive_fail" -eq 0 ]; then echo M80_ARCHIVES_PASS; else echo M80_ARCHIVES_FAIL; fi
if [ "$data_fail" -eq 0 ]; then echo M80_SQLITE_BC_PASS; else echo M80_SQLITE_BC_FAIL; fi

# Nano is present in the signed APK input but not counted as tested until a
# genuine PTY interaction/terminal editor oracle exists. `--version` is not it.
echo M80_NANO_DEFERRED

if [ "$failures" -eq 0 ]; then
    echo M80_BATCH_PASS
else
    echo "[M80 BATCH FAIL] $failures/19 actual Linux program cases failed" >&2
    echo M80_BATCH_FAIL
    exit 1
fi
