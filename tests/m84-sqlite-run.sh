#!/bin/sh
# Original SQLite 3.54.1 is built by signed native GNU GCC/Make INSIDE NaSeed.
# Reuse the complete, already-proven M57 original configure/build/SQL oracle;
# only the source extraction, ext4 destination and cold-boot replay are new.
# Neither SQLite's original source nor its generated Makefile is patched.
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG

fail() { echo "[M84 SQLITE FAIL] $*" >&2; exit 1; }

mounted_ext4_rw() {
    [ -r /proc/mounts ] || return 1
    while read -r dev path fs mode rest; do
        [ "$path" = /mnt ] || continue
        [ "$fs" = lwext4 ] && [ "$mode" = rw ]
        return $?
    done < /proc/mounts
    return 1
}

src=/mnt/m84-sqlite
db=/mnt/m84-sqlite.db
sqlite=$src/sqlite3

signed_tools() {
    mounted_ext4_rw || fail 'expected writable real VirtIO ext4 at /mnt'
    for executable in /usr/bin/gcc /usr/bin/make /usr/bin/as \
                      /usr/bin/ld /usr/bin/readelf; do
        [ -x "$executable" ] || fail "missing signed native tool $executable"
    done
    /usr/bin/make --version | /bin/busybox grep -q '^GNU Make ' ||
        fail 'not the original signed GNU Make'
    /usr/bin/gcc --version | /bin/busybox grep -q gcc ||
        fail 'not the original signed GCC'
    test -s /usr/lib/libc.a || fail 'missing original musl development files'
    echo M84_SQLITE_SIGNED_NATIVE_TOOLCHAIN_PASS
}

verify_persistent_sql() {
    [ -x "$sqlite" ] && [ -s "$db" ] ||
        fail 'guest-built SQLite CLI or persistent database is missing'
    [ "$("$sqlite" :memory: 'SELECT sqlite_version();')" = 3.54.1 ] ||
        fail 'guest-built SQLite version changed'
    [ "$("$sqlite" "$db" 'SELECT COUNT(*) FROM person;')" = 2 ] ||
        fail 'committed SQLite rows not preserved'
    [ "$("$sqlite" "$db" 'SELECT SUM(points) FROM scores;')" = 33 ] ||
        fail 'SQLite values not preserved'
    [ "$("$sqlite" "$db" \
      "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_scores_person';")" = 1 ] ||
        fail 'SQLite index missing'
    expected=$(printf 'alpha|11\nbeta|22')
    actual=$("$sqlite" "$db" \
      'SELECT person.name, scores.points FROM person JOIN scores ON person.id=scores.person_id ORDER BY person.id;')
    [ "$actual" = "$expected" ] || fail 'SQLite relational join after reboot differs'
    [ "$("$sqlite" "$db" 'PRAGMA integrity_check;')" = ok ] ||
        fail 'SQLite database integrity check failed'
    [ "$("$sqlite" "$db" 'PRAGMA journal_mode;')" = delete ] ||
        fail 'original SQLite DELETE journal mode changed'
    [ ! -e "$db-journal" ] || fail 'stale SQLite rollback journal'
}

build_mode() {
    signed_tools
    [ -s /tests/m84-sqlite.tar.gz ] || fail 'missing publisher-verified original source archive'
    [ ! -e "$src" ] && [ ! -e "$db" ] ||
        fail 'build mode unexpectedly found old source/database on physical disk'
    # The official SQLite archive contains exactly one top-level directory.
    # BusyBox -m restores extraction-time mtimes instead of the tarball's
    # 2026 timestamps, which may be in the future relative to NaSeed RTC.
    /bin/busybox tar -mzxf /tests/m84-sqlite.tar.gz -C /mnt ||
        fail 'untouched official SQLite archive extraction'
    test -s /mnt/sqlite-autoconf-3540100/configure &&
        test -s /mnt/sqlite-autoconf-3540100/sqlite3.c &&
        test -s /mnt/sqlite-autoconf-3540100/shell.c ||
        fail 'original SQLite source release contents missing'
    /bin/busybox mv /mnt/sqlite-autoconf-3540100 "$src" ||
        fail 'rename original extracted source into persistent build tree'
    [ ! -e "$src/sqlite3" ] || fail 'SQLite CLI was prebuilt outside NaSeed'
    echo M84_SQLITE_ORIGINAL_SOURCE_ON_EXT4_PASS

    # M57's DEFAULT behavior remains unchanged on tmpfs. Its original
    # upstream/SQL oracle is selected here with an ext4 worktree and DB,
    # without duplicating its SQL validation or its native compile steps.
    NAS_SQLITE_ORIGINAL="$src" \
    NAS_SQLITE_WORK="$src" \
    NAS_SQLITE_DB="$db" \
    NAS_SQLITE_SKIP_LARGEFILE=1 \
    NAS_SQLITE_SKIP_SOURCE_COPY=1 \
        /bin/sh /tests/m57-sqlite-run.sh ||
        fail 'existing M57 original GCC/Make/SQLite transactional oracle'
    echo M84_SQLITE_NATIVE_BUILD_SQL_ORACLE_PASS

    verify_persistent_sql
    ( cd "$src" && /usr/bin/make --no-print-directory -q sqlite3 ) ||
        fail 'original GNU Make did not report a clean SQLite build'
    echo M84_SQLITE_INCREMENTAL_NOOP_PASS
    /bin/busybox sync || fail 'cannot flush ext4 SQLite database/build'
    echo M84_SQLITE_EXT4_PERSISTENCE_PASS
    echo M84_SQLITE_BUILD_PASS
}

replay_mode() {
    signed_tools
    [ -s "$src/Makefile" ] && [ -s "$src/sqlite3.c" ] &&
        [ -x "$sqlite" ] && [ -s "$db" ] ||
        fail 'original source/build/SQLite DB did not survive cold boot'
    verify_persistent_sql
    [ "$("$sqlite" "$db" \
      'BEGIN IMMEDIATE; UPDATE scores SET points=999 WHERE person_id=1; ROLLBACK; SELECT points FROM scores WHERE person_id=1;')" = 11 ] ||
        fail 'SQLite rollback on previously persisted database is not atomic'
    verify_persistent_sql
    echo M84_SQLITE_REPLAY_DB_INTEGRITY_PASS
    ( cd "$src" && /usr/bin/make --no-print-directory -q sqlite3 ) ||
        fail 'persistent original GNU Make SQLite build is not a no-op'
    echo M84_SQLITE_REPLAY_MAKE_NOOP_PASS
    echo M84_SQLITE_REPLAY_PASS
}

case "${1:-}" in
    --build) [ "$#" -eq 1 ] || exit 2; build_mode ;;
    --replay) [ "$#" -eq 1 ] || exit 2; replay_mode ;;
    *) echo 'usage: m84-sqlite-run.sh --build|--replay' >&2; exit 2 ;;
esac
