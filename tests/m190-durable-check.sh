#!/bin/sh
# Same real ext4 bytes before AND after a full kernel reboot. No file creation.
set -eu
target=${M190_TEST_TARGET:-/async/m190-editor.txt}
[ -f "$target" ] && [ ! -L "$target" ] || {
    echo '[M190 FAIL] physical ext4 editor inode absent' >&2; exit 1;
}
size=$(/bin/busybox wc -c < "$target")
observed=$(/bin/busybox cat "$target"; /bin/busybox printf x)
expected=$(/bin/busybox printf 'm190vimoriginal\nm190nanopersist\nx')
[ "$size" -eq 32 ] && [ "$observed" = "$expected" ] || {
    echo '[M190 FAIL] physical ext4 editor bytes mismatch' >&2; exit 1;
}
echo M190_DURABLE_EDITOR_CONTENT_VERIFIED
