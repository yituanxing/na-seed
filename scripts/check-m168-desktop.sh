#!/bin/sh
# Real production source/link host gates, not a substitute for native QEMU.
set -eu
work=$(mktemp -d "${TMPDIR:-/tmp}/naseed-m168-desktop.XXXXXX")
trap 'rm -f "$work/naseed-desktop" "$work/naseed-image" "$work/naseed-terminal" "$work/naseed-browser" "$work/output"; rmdir "$work"' EXIT HUP INT TERM
for app in naseed-desktop naseed-image naseed-terminal naseed-browser; do
    gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$work/$app"
    readelf -h "$work/$app" | grep -Fq 'Advanced Micro Devices X86-64'
    ! readelf -l "$work/$app" | grep -Fq INTERP
done
"$work/naseed-desktop" --selftest168 > "$work/output" 2>&1 || {
    sed -n '1,70p' "$work/output" >&2
    exit 1
}
grep -Fxq '[M167 HOST DESKTOP PASS] fork/SCM_RIGHTS/memfd shared pixels, overlap, focus, hit-testing, axis scaling, live surface update' "$work/output"
grep -Fxq '[M168 HOST DESKTOP PASS] three registered dimensioned clients, z-order, focus, surface pixels and userspace text glyphs' "$work/output"
echo '[M168 HOST PROGRAM PASS] four static ELF executables, metadata, triple z-order and real userspace glyphs'
