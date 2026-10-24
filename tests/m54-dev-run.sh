#!/bin/sh
# Same native QEMU boot: Vim -> Git -> GNU Make -j2 -> real GCC -> executable
# -> original CPython subprocess/SQLite -> Git commit; repeat incrementally.
set -eu
stage=init
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] M54 stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
GIT_CONFIG_NOSYSTEM=1
GIT_TERMINAL_PROMPT=0
GIT_PAGER=cat
PYTHONHOME=/usr
PYTHONDONTWRITEBYTECODE=1
PYTHONUNBUFFERED=1
export PATH HOME LC_ALL LANG TERM GIT_CONFIG_NOSYSTEM GIT_TERMINAL_PROMPT GIT_PAGER
export PYTHONHOME PYTHONDONTWRITEBYTECODE PYTHONUNBUFFERED

for tool in /usr/bin/gcc /usr/bin/make /usr/bin/git /usr/bin/vim /usr/bin/python3; do
    test -x "$tool"
done
/usr/bin/gcc --version | /bin/busybox grep -Fq gcc
/usr/bin/make --version | /bin/busybox grep -Fq 'GNU Make'
/usr/bin/git --version | /bin/busybox grep -Fq 'git version'
/usr/bin/vim --version | /bin/busybox grep -Fq 'VIM - Vi IMproved'
/usr/bin/python3 --version | /bin/busybox grep -Fq 'Python 3.'
echo M54_ORIGINAL_FIVE_NATIVE_TOOLS_PASS

stage=create-git-repository
root=/tmp/m54-project
/bin/busybox mkdir -p "$root" /tmp/m54-empty-template
/bin/busybox cp /tests/m54-src/answer.c /tests/m54-src/answer.h "$root/"
/bin/busybox cp /tests/m54-src/main.c /tests/m54-src/Makefile "$root/"
/bin/busybox cp /tests/m54-src/.gitignore "$root/"
/usr/bin/git -C "$root" init -q --template=/tmp/m54-empty-template
/usr/bin/git -C "$root" config user.name "NaSeed Builder"
/usr/bin/git -C "$root" config user.email "naseed@example.invalid"
/usr/bin/git -C "$root" add answer.c answer.h main.c Makefile .gitignore
/usr/bin/git -C "$root" commit -qm "initial source"
test "$(/usr/bin/git -C "$root" rev-list --count HEAD)" = 1
echo M54_GIT_INITIAL_SOURCES_PASS

stage=vim-first-edit
/usr/bin/vim -Nu NONE -i NONE -n -N -es "$root/answer.c" \
    -c '%s/return 1;/return 7;/' -c 'wq'
/usr/bin/git -C "$root" diff -- answer.c > /tmp/m54-first-diff
/bin/busybox grep -Fq '+    return 7;' /tmp/m54-first-diff
echo M54_VIM_EDIT_GIT_DIFF_PASS

stage=parallel-guest-make
cd "$root"
/usr/bin/make -j2 all
/bin/busybox chmod 755 app
test "$(./app)" = 'M54-APP answer=7'
echo M54_GNU_MAKE_GCC_LINK_RUN_PASS

stage=python-first-validation
/usr/bin/python3 -B /tests/dev-check.py 7
/usr/bin/git add answer.c
/usr/bin/git commit -qm "Vim changed answer to seven"
test "$(/usr/bin/git rev-list --count HEAD)" = 2
test -z "$(/usr/bin/git status --porcelain)"
echo M54_GIT_COMMIT_BUILT_SOURCE_PASS

stage=incremental-vim-edit
/bin/busybox sleep 1
/usr/bin/vim -Nu NONE -i NONE -n -N -es "$root/answer.c" \
    -c '%s/return 7;/return 9;/' -c 'wq'
/usr/bin/git diff -- answer.c > /tmp/m54-second-diff
/bin/busybox grep -Fq '+    return 9;' /tmp/m54-second-diff
/usr/bin/make -j2 all
test "$(./app)" = 'M54-APP answer=9'
echo M54_INCREMENTAL_VIM_MAKE_REBUILD_PASS

stage=python-second-validation
/usr/bin/python3 -B /tests/dev-check.py 9
/usr/bin/git add answer.c
/usr/bin/git commit -qm "Vim changed answer to nine"
test "$(/usr/bin/git rev-list --count HEAD)" = 3
test "$(/usr/bin/git log -1 --format=%s)" = "Vim changed answer to nine"
test -z "$(/usr/bin/git status --porcelain)"
test "$(/usr/bin/git show HEAD:answer.c | /bin/busybox grep -c 'return 9;')" = 1
echo M54_DEV_FULL_LIFECYCLE_PASS
