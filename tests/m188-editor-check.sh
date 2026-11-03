#!/bin/sh
# Execute ONLY after a human/QMP-driven live GUI PTY Vim session has written
# /tmp/m188-vim.txt. This script does not launch Vim or create the target.
set -eu
target=/tmp/m188-vim.txt
[ -f "$target" ] && [ ! -L "$target" ] || {
    echo '[M188 VIM EDIT FAIL] real Vim-saved regular file absent' >&2
    exit 1
}
# The three-ISA BusyBox is deliberately built from allnoconfig; `cmp` is
# NOT a selected applet. Use only the verified cat/wc/printf applets. The
# sentinel preserves the final LF through command substitution; exact byte
# count independently excludes extra NULs or trailing lines.
bytes=$(/bin/busybox wc -c < "$target")
observed=$(/bin/busybox cat "$target"; /bin/busybox printf 'x')
expected=$(/bin/busybox printf 'm188vimrealinput\nx')
[ "$bytes" -eq 17 ] && [ "$observed" = "$expected" ] || {
    echo '[M188 VIM EDIT FAIL] saved bytes do not equal one m188vimrealinput line' >&2
    exit 1
}
echo M188_VIM_EDITED_CONTENT_VERIFIED
