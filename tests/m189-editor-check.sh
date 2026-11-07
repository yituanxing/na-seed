#!/bin/sh
# Run ONLY after original Vim -> original Nano saved the SAME live PTY file.
# The guest invocation uses the fixed real path. The override is solely for
# the isolated host negative/positive test and is not set by the QMP run.
set -eu
target=${M189_TEST_TARGET:-/tmp/m188-vim.txt}
[ -f "$target" ] && [ ! -L "$target" ] || {
    echo '[M189 EDIT FAIL] original Vim/Nano regular file missing' >&2
    exit 1
}
bytes=$(/bin/busybox wc -c < "$target")
observed=$(/bin/busybox cat "$target"; /bin/busybox printf x)
expected=$(/bin/busybox printf 'm188vimrealinput\nm189nanoroundtrip\nx')
[ "$bytes" -eq 35 ] && [ "$observed" = "$expected" ] || {
    echo '[M189 EDIT FAIL] Nano did not preserve and append exact Vim bytes' >&2
    exit 1
}
echo M189_NANO_EDITED_CONTENT_VERIFIED
