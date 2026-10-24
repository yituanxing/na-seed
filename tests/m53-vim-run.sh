#!/bin/sh
# Vim Ex mode actually opens, modifies and writes files to NaSeed tmpfs.
# This is NOT the interactive acceptance: the separate UART test is mandatory.
set -eu
stage=version
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[M53 FAIL] ex stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
TERM=vt100
LC_ALL=C
export PATH HOME TERM LC_ALL
vim=/usr/bin/vim
"$vim" --version | /bin/busybox grep -Fq 'VIM - Vi IMproved'
echo M53_ORIGINAL_SIGNED_VIM_PASS
stage=ex-edit-write
printf 'M53 original Vim ex editor\nbeta\n' > /tmp/m53-ex.txt
# Original Vim :setline, :substitute, :write, :quit, no custom editor logic.
"$vim" -Nu NONE -i NONE -n -N -es /tmp/m53-ex.txt \
    -c 'call setline(1, "M53 vim modified from Ex")' \
    -c '%s/beta/gamma/g' -c 'write' -c 'quit'
printf 'M53 vim modified from Ex\ngamma\n' > /tmp/m53-expected.txt
/bin/busybox cmp -s /tmp/m53-ex.txt /tmp/m53-expected.txt
echo M53_VIM_REAL_EX_FILE_SAVE_PASS
stage=ex-reopen
"$vim" -Nu NONE -i NONE -n -N -es /tmp/m53-ex.txt \
    -c 'call append("$", "M53 vim reopen append")' -c 'wq'
/bin/busybox grep -Fxq 'M53 vim reopen append' /tmp/m53-ex.txt
echo M53_VIM_REOPEN_WRITE_PASS
echo M53_VIM_EX_SUITE_PASS
