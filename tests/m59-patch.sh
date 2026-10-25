#!/bin/sh
set -eu
test -x /usr/bin/diff
test -x /usr/bin/patch
/bin/busybox mkdir -p /tmp/m59-patch
cd /tmp/m59-patch
printf 'first line\nsecond line\n' > target.txt
printf 'first line\nchanged line\nthird line\n' > expected.txt
if /usr/bin/diff -u --label target.txt --label target.txt \
    target.txt expected.txt > change.patch; then
    echo '[M59 FAIL] genuine diff wrongly claimed identical content' >&2
    exit 1
else
    test "$?" -eq 1
fi
/bin/busybox grep -Fq '+changed line' change.patch
echo M59_DIFF_UNIFIED_PASS
/usr/bin/patch --batch -p0 < change.patch
/bin/busybox cmp -s target.txt expected.txt
echo M59_PATCH_APPLY_PASS
