#!/bin/sh
# Test only ORIGINAL GNU ELF from signed Alpine APKs, no guest compilation.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
select_gnu() {
    program=$1 banner=$2
    for candidate in "/usr/bin/$program" "/bin/$program"; do
        if [ -x "$candidate" ] &&
           "$candidate" --version | /bin/busybox grep -Fq "$banner"; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M41 FAIL] genuine GNU $program not found" >&2
    return 1
}
findcmd=$(select_gnu find 'GNU findutils')
grepcmd=$(select_gnu grep 'GNU grep')
sedcmd=$(select_gnu sed 'GNU sed')
sortcmd=$(select_gnu sort 'GNU coreutils')
diffcmd=$(select_gnu diff 'GNU diffutils')
cpcmd=$(select_gnu cp 'GNU coreutils')
mvcmd=$(select_gnu mv 'GNU coreutils')
shacmd=$(select_gnu sha256sum 'GNU coreutils')
echo M41_GNU_ELF_PASS

# Copy CPIO read-only input into true writable nested tmpfs inodes.
/bin/busybox mkdir -p /tmp/m41-work/inbox/nested
"$cpcmd" /tests/m41/raw/a.txt /tmp/m41-work/inbox/a.txt
"$cpcmd" /tests/m41/raw/nested/b.txt /tmp/m41-work/inbox/nested/b.txt
/bin/busybox cmp /tests/m41/raw/a.txt /tmp/m41-work/inbox/a.txt
/bin/busybox cmp /tests/m41/raw/nested/b.txt /tmp/m41-work/inbox/nested/b.txt
echo M41_COPY_PASS

# GNU find forks/execs genuine GNU grep. Unix pipes cross executable
# boundaries into GNU sed/sort, checked against independent CPIO fixture.
"$findcmd" /tmp/m41-work/inbox -type f -name '*.txt' \
    -exec "$grepcmd" -h '^WARN ' {} + |
    "$sedcmd" 's/^WARN //' |
    "$sortcmd" > /tmp/m41-work/output.txt
"$diffcmd" -u /tests/m41/expected.txt /tmp/m41-work/output.txt
echo M41_FIND_EXEC_PASS
echo M41_PIPELINE_PASS

"$mvcmd" /tmp/m41-work/output.txt /tmp/m41-work/final.txt
test ! -e /tmp/m41-work/output.txt
/bin/busybox cmp /tests/m41/expected.txt /tmp/m41-work/final.txt
checksum=$("$shacmd" /tmp/m41-work/final.txt | /bin/busybox cut -d ' ' -f 1)
test "$checksum" = '9f6ee4fc556bce89e031470bb4918324fd764808a1f9782a50e53abb42544ca3'
echo M41_RENAME_SHA256_PASS

# Distinguish intentional difference (exit 1) from a tool/kernel error (2).
if "$diffcmd" -q /tests/m41/raw/a.txt /tmp/m41-work/final.txt \
    > /tmp/m41-work/negative-diff.txt; then
    echo '[M41 FAIL] GNU diff falsely returned equality' >&2
    exit 1
else
    rc=$?
    test "$rc" -eq 1
fi
echo M41_DIFF_EXIT_PASS
echo M41_UNIX_PASS
