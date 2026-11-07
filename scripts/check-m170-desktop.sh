#!/bin/sh
# Host production C/ABI gate. Native QEMU follows in a separate action.
set -eu
sh scripts/check-m168-desktop.sh
work=$(mktemp -d /tmp/naseed-m170.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for app in naseed-desktop naseed-terminal; do
    gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
      "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
      -o "$work/$app"
done
"$work/naseed-terminal" --selftest170 > "$work/terminal.log" 2>&1 || {
    cat "$work/terminal.log" >&2; exit 1;
}
"$work/naseed-desktop" --selftest170 > "$work/desktop.log" 2>&1 || {
    cat "$work/desktop.log" >&2; exit 1;
}
grep -Fq '[M170 HOST TERMINAL PASS]' "$work/terminal.log"
grep -Fq '[M170 HOST DESKTOP PASS]' "$work/desktop.log"
echo '[M170 HOST PASS] legacy gates, PTY transcript, crashed child isolation'
