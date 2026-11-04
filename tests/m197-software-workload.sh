#!/bin/bash
# One original, public-APK-installed four-program application workflow.
# Both modes run INSIDE NaSeed; host creates only this verification script.
set -euo pipefail
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C
export GIT_CONFIG_NOSYSTEM=1 GIT_TERMINAL_PROMPT=0 GIT_PAGER=cat

work=/root/m197-app
fail() { echo "[M197 SOFTWARE FAIL] $*" >&2; exit 1; }
check_programs() {
    for program in /bin/bash /usr/bin/lua5.4 /usr/bin/sqlite3 /usr/bin/git; do
        [[ -x "$program" ]] || fail "missing public installed $program"
    done
    [[ "$(/usr/bin/git --version)" == git\ version\ * ]] || fail 'original git banner'
    /usr/bin/lua5.4 -v 2>&1 | /bin/busybox grep -Fq 'Lua 5.4' || fail 'original Lua 5.4'
}
check_data() {
    [[ -s "$work/records.txt" && -s "$work/summary.txt" &&
       -s "$work/ledger.db" ]] || fail 'missing persistent application bytes'
    [[ "$(/bin/busybox cat "$work/summary.txt")" == total=66 ]] || fail 'Lua total'
    [[ "$(/usr/bin/sqlite3 "$work/ledger.db" 'SELECT COUNT(*) FROM ledger;')" == 3 ]] ||
        fail 'SQLite row count'
    [[ "$(/usr/bin/sqlite3 "$work/ledger.db" 'SELECT SUM(points) FROM ledger;')" == 66 ]] ||
        fail 'SQLite relational sum'
    [[ "$(/usr/bin/sqlite3 "$work/ledger.db" 'PRAGMA integrity_check;')" == ok ]] ||
        fail 'SQLite disk integrity'
    [[ "$(/usr/bin/sqlite3 "$work/ledger.db" 'PRAGMA journal_mode;')" == delete ]] ||
        fail 'unexpected SQLite journal mode'
    [[ ! -e "$work/ledger.db-journal" ]] || fail 'SQLite journal not retired'
    [[ "$(/usr/bin/git -C "$work" rev-list --count HEAD)" == 1 ]] ||
        fail 'Git object history'
    [[ "$(/usr/bin/git -C "$work" log -1 --format=%s)" == 'NaSeed M197 public app' ]] ||
        fail 'Git durable commit'
    [[ "$(/usr/bin/git -C "$work" show HEAD:summary.txt)" == total=66 ]] ||
        fail 'Git committed Lua result'
    [[ -z "$(/usr/bin/git -C "$work" status --porcelain --untracked-files=no)" ]] ||
        fail 'Git tracked worktree dirty'
}

case "${1:-}" in
    --first)
        [[ ! -e "$work" ]] || fail 'application data preseeded before guest work'
        check_programs
        /bin/busybox mkdir -p "$work" "$work/empty-git-template"
        /bin/busybox printf 'alpha 11\nbeta 22\ngamma 33\n' > "$work/records.txt"
        # An actual Bash background child, wait status and separate pipe.
        /bin/busybox cat "$work/records.txt" > "$work/copy.txt" &
        worker=$!
        wait "$worker"
        /bin/busybox cmp -s "$work/records.txt" "$work/copy.txt" ||
            fail 'Bash background writer result'
        [[ "$(/bin/busybox grep -c '^[a-z]' "$work/copy.txt")" == 3 ]] ||
            fail 'Bash pipeline result'
        echo M197_BASH_PROCESS_PIPE_PASS

        /bin/busybox cat > "$work/transform.lua" <<'LUA'
local source = assert(io.open(arg[1], "r"))
local total, count = 0, 0
for line in source:lines() do
    local name, points = line:match("^(%a+)%s+(%d+)$")
    assert(name and points)
    total, count = total + tonumber(points), count + 1
end
source:close()
assert(count == 3 and total == 66)
local output = assert(io.open(arg[2], "w"))
assert(output:write("total=" .. total .. "\n"))
assert(output:close())
LUA
        /usr/bin/lua5.4 "$work/transform.lua" "$work/records.txt" "$work/summary.txt"
        [[ "$(/bin/busybox cat "$work/summary.txt")" == total=66 ]] ||
            fail 'original Lua VM and host-visible file I/O'
        echo M197_LUA_VM_FILE_PASS

        /usr/bin/sqlite3 "$work/ledger.db" \
            "PRAGMA journal_mode=DELETE; CREATE TABLE ledger(name TEXT PRIMARY KEY, points INTEGER NOT NULL); BEGIN IMMEDIATE; INSERT INTO ledger VALUES('alpha',11),('beta',22),('gamma',33); COMMIT;" \
            > "$work/sqlite-journal-mode.txt"
        [[ "$(/bin/busybox cat "$work/sqlite-journal-mode.txt")" == delete ]] ||
            fail 'SQLite journal selection'
        [[ "$(/usr/bin/sqlite3 "$work/ledger.db" "BEGIN IMMEDIATE; UPDATE ledger SET points=999 WHERE name='alpha'; ROLLBACK; SELECT points FROM ledger WHERE name='alpha';")" == 11 ]] ||
            fail 'SQLite transactional rollback'
        echo M197_SQLITE_EXT4_TRANSACTION_PASS

        /usr/bin/git -C "$work" init -q --template="$work/empty-git-template"
        /usr/bin/git -C "$work" config user.name 'NaSeed Public Cohort'
        /usr/bin/git -C "$work" config user.email 'naseed@example.invalid'
        /usr/bin/git -C "$work" add records.txt summary.txt transform.lua ledger.db
        /usr/bin/git -C "$work" commit -qm 'NaSeed M197 public app'
        check_data
        echo M197_GIT_OBJECT_AND_SQLITE_INTEGRITY_PASS
        /bin/busybox printf 'M197_PUBLIC_ALPINE_SOFTWARE_2026\n' > /root/m197-wan.txt
        /bin/busybox sync
        echo M197_APP_WORKLOAD_PASS
        ;;
    --reopen)
        check_programs
        check_data
        [[ "$(/bin/busybox cat /root/m197-wan.txt)" == M197_PUBLIC_ALPINE_SOFTWARE_2026 ]] ||
            fail 'missing durable first-boot witness'
        /usr/bin/lua5.4 "$work/transform.lua" "$work/records.txt" /tmp/m197-recheck.txt
        /bin/busybox cmp -s "$work/summary.txt" /tmp/m197-recheck.txt ||
            fail 'new QEMU replay of original Lua VM differs'
        echo M197_REOPEN_WORKLOAD_PASS
        ;;
    *) fail 'usage: /bin/bash /root/m197-software-workload.sh --first|--reopen' ;;
esac
