#!/bin/sh
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
export PATH HOME LC_ALL LANG TERM
if ! /bin/bash --noprofile --norc /tests/m79-workflow.bash; then
  echo '[M79 FAIL] original Bash/Vim/Git/Python workflow did not complete' >&2
  exit 1
fi
echo M79_FOUR_ORIGINAL_PROGRAMS_PASS
