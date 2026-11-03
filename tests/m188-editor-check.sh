#!/bin/sh
# Execute ONLY after a human/QMP-driven live GUI PTY Vim session has written
# /tmp/m188-vim.txt. This script does not launch Vim or create the target.
set -eu
target=/tmp/m188-vim.txt
expected=/tmp/m188-vim-expected.txt
[ -f "$target" ] && [ ! -L "$target" ] || {
    echo '[M188 VIM EDIT FAIL] real Vim-saved regular file absent' >&2
    exit 1
}
# Compare exact bytes (one ASCII line and exactly one LF), not merely grep.
/bin/busybox printf 'm188vimrealinput\n' > "$expected"
/bin/busybox cmp -s "$expected" "$target" || {
    echo '[M188 VIM EDIT FAIL] saved bytes do not equal one m188vimrealinput line' >&2
    exit 1
}
echo M188_VIM_EDITED_CONTENT_VERIFIED
