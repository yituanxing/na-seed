#!/bin/sh
# Exercise production multi-process desktop IPC/compositor code on Linux host.
# The QEMU acceptance separately proves the NaSeed ABI, hardware and pixels.
set -eu
work=$(mktemp -d "${TMPDIR:-/tmp}/naseed-m167-desktop.XXXXXX")
trap 'rm -f "$work/naseed-desktop" "$work/naseed-image" "$work/naseed-terminal" "$work/output"; rmdir "$work"' EXIT HUP INT TERM
for app in naseed-desktop naseed-image naseed-terminal; do
    gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$work/$app"
    readelf -h "$work/$app" | grep -Fq 'Advanced Micro Devices X86-64'
    ! readelf -l "$work/$app" | grep -Fq INTERP
done
"$work/naseed-desktop" --selftest > "$work/output" 2>&1 || {
    sed -n '1,80p' "$work/output" >&2
    exit 1
}
grep -Fxq '[M167 HOST DESKTOP PASS] fork/SCM_RIGHTS/memfd shared pixels, overlap, focus, hit-testing, axis scaling, live surface update' "$work/output" || {
    sed -n '1,80p' "$work/output" >&2
    exit 1
}
echo '[M167 HOST PROGRAM PASS] three distinct fully static ELF links, production compositor IPC/focus/memfd selftest'
