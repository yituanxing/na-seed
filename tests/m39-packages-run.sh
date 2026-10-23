#!/bin/sh
# Pure runtime acceptance, three unmodified Alpine APK applications:
# Lua 5.4, jq and SQLite CLI. No compiler/build tools inside NaSeed.
set -eu
echo M39_PACKAGES_START
/usr/bin/lua5.4 /tests/lua-vm.lua
/usr/bin/lua5.4 /tests/lua-io.lua
echo M39_LUA_PASS

score=$(/usr/bin/jq -r '[.jobs[] | select(.enabled) | .score] | add' /tests/jobs.json)
test "$score" = 42
echo M39_JQ_PASS

test "$(/usr/bin/sqlite3 :memory: 'SELECT 21+21;')" = 42
# Create a real database using the existing shared tmpfs and libc/VFS.
# A second original SQLite process reopens it to confirm persistence in RAM.
/usr/bin/sqlite3 /tmp/m39.db 'CREATE TABLE items(id INTEGER PRIMARY KEY,value INT); INSERT INTO items(value) VALUES(20),(22);'
test "$(/usr/bin/sqlite3 /tmp/m39.db 'SELECT SUM(value) FROM items;')" = 42
echo M39_SQLITE_PASS
echo M39_PACKAGES_PASS
