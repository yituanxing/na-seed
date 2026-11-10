#!/bin/sh
# Host VT ABI and keyboard checks; physical VirtIO/QMP remains the native gate.
set -eu
test -f kernel/drivers/vt.c || {
    echo 'run from the NaSeed repository root' >&2
    exit 2
}
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naseed-m202-vt.XXXXXXXX")
trap 'rm -f "$temp_dir/m202-vt-host"; rmdir "$temp_dir"' EXIT HUP INT TERM

gcc -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -I kernel -fsyntax-only kernel/drivers/vt.c kernel/drivers/tty.c
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
    -DNAS_ENABLE_VIRTIO_INPUT=1 -I . -I kernel \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    scripts/test-m202-vt.c kernel/drivers/vt.c kernel/drivers/tty_ldisc.c \
    -o "$temp_dir/m202-vt-host"
ASAN_OPTIONS=detect_leaks=0 "$temp_dir/m202-vt-host"
