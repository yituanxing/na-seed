#!/bin/sh
# Entire official SQLite 3.54.1 configure/Make/GCC/SQL lifecycle INSIDE NaSeed.
# No Alpine-packaged sqlite3 executable is under test. No SQLite source patch.
set -eu
stage=verify-source
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] M57 stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
original=/tests/m57-source
src=/tmp/m57-sqlite
test -s "$original/configure"
test -s "$original/sqlite3.c"
test -s "$original/shell.c"
test ! -e "$original/sqlite3"
for tool in /usr/bin/gcc /usr/bin/make /usr/bin/as /usr/bin/ld /usr/bin/readelf; do
    test -x "$tool"
done
echo M57_GENUINE_UPSTREAM_SOURCE_PASS

stage=copy-upstream-to-tmpfs
/bin/busybox cp -R "$original" "$src"
test -s "$src/configure"
cd "$src"

stage=real-original-sqlite-configure
# Explicit CFLAGS controls optimisation cost on slow software QEMU.
# Original configure/make scripts and source bytes remain untouched.
CFLAGS='-O0' ./configure
test -s Makefile
echo M57_NATIVE_SQLITE_CONFIGURE_PASS

stage=real-guest-make-gcc-link
echo M57_REAL_NATIVE_SQLITE_BUILD_BEGIN
/usr/bin/make -j2 sqlite3
test -x "$src/sqlite3"
test -s "$src/sqlite3"
# Link may produce ET_EXEC or PIE ET_DYN; real execution is authoritative.
/usr/bin/readelf -h "$src/sqlite3" | /bin/busybox grep -Eq 'Type:.*(EXEC|DYN)'
echo M57_NATIVE_SQLITE_COMPILE_LINK_PASS

stage=new-sqlite-cli-executes
sqlite="$src/sqlite3"
test "$("$sqlite" :memory: 'SELECT sqlite_version();')" = '3.54.1'
test "$("$sqlite" :memory: 'SELECT 7*6;')" = 42
echo M57_NEW_SQLITE_CLI_EXEC_PASS

stage=file-backed-database-commit
db=/tmp/m57-real-sqlite.db
test ! -e "$db"
# Actual DELETE rollback journal, not memory/temp-only SQL and not WAL;
# WAL's cross-process file-backed MAP_SHARED is a separate kernel capability.
test "$("$sqlite" "$db" 'PRAGMA journal_mode=DELETE;')" = delete
"$sqlite" "$db" "CREATE TABLE person (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL);
 CREATE TABLE scores (person_id INTEGER NOT NULL, points INTEGER NOT NULL);
 CREATE INDEX idx_scores_person ON scores(person_id);
 BEGIN IMMEDIATE;
 INSERT INTO person(id,name) VALUES (1,'alpha'),(2,'beta');
 INSERT INTO scores(person_id,points) VALUES (1,11),(2,22);
 COMMIT;"
test -s "$db"
echo M57_FILE_DB_CREATE_COMMIT_PASS

stage=separate-process-reopen
test "$("$sqlite" "$db" 'SELECT COUNT(*) FROM person;')" = 2
test "$("$sqlite" "$db" 'SELECT SUM(points) FROM scores;')" = 33
echo M57_FILE_DB_REOPEN_QUERY_PASS

stage=rollback-atomicity
test "$("$sqlite" "$db" \
 'BEGIN IMMEDIATE; UPDATE scores SET points=999 WHERE person_id=1; ROLLBACK; SELECT points FROM scores WHERE person_id=1;')" = 11
test "$("$sqlite" "$db" \
 'BEGIN; INSERT INTO scores(person_id,points) VALUES(2,999); ROLLBACK; SELECT COUNT(*) FROM scores;')" = 2
echo M57_TRANSACTION_ROLLBACK_PASS

stage=real-index-and-join
test "$("$sqlite" "$db" \
 "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_scores_person';")" = 1
rows=$("$sqlite" "$db" \
 'SELECT person.name, scores.points FROM person JOIN scores ON person.id=scores.person_id ORDER BY person.id;')
expected=$(printf 'alpha|11\nbeta|22')
test "$rows" = "$expected"
echo M57_SQLITE_INDEX_JOIN_PASS

stage=integrity-check
test "$("$sqlite" "$db" 'PRAGMA integrity_check;')" = ok
test "$("$sqlite" "$db" 'PRAGMA journal_mode;')" = delete
test ! -e "$db-journal"
echo M57_SQLITE_INTEGRITY_PASS
echo M57_NATIVE_SQLITE_FULL_PASS
