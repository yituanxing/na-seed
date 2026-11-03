#!/bin/sh
# Focused M173 host gate.  It exercises production Bus/device resource and
# VirtIO ports, existing /dev, PTY and LVGL, then the dynamic AF_UNIX desktop
# path.  Native PCI/MMIO hardware and pixels are verified separately in QEMU.
set -eu
for script in scripts/check-m173-batch.sh \
              scripts/stage-m173-dynamic-client.sh \
              scripts/probe-m173-batch.sh; do
    sh -n "$script"
done
python3 -m py_compile scripts/m173-qmp-verify.py
python3 scripts/check-three-isa-core.py
python3 scripts/check-architecture-boundaries.py

# The shared typed bus resource changes impact U-Boot block/net and the
# native VirtIO-input binder, so retain the real original-source port gates.
sh scripts/check-block-foundation.sh
sh scripts/check-m172-device-display.sh

# M171 host gate builds real upstream LVGL v9.4, not a stub. Keep all prior
# deterministic desktop/PTY/protocol selftests before new M173 behavior.
sh scripts/check-m171-desktop.sh
work=$(mktemp -d /tmp/naseed-m173-host.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    user/desktop/naseed-desktop.c user/desktop/protocol.c \
    user/desktop/text.c -o "$work/naseed-desktop"
"$work/naseed-desktop" --selftest173proto > "$work/protocol.log" 2>&1 || {
    cat "$work/protocol.log" >&2; exit 1;
}
grep -Fq '[M173 HOST PROTOCOL PASS]' "$work/protocol.log"
"$work/naseed-desktop" --selftest173 > "$work/client.log" 2>&1 || {
    cat "$work/client.log" >&2; exit 1;
}
grep -Fq '[M173 HOST DYNAMIC PASS]' "$work/client.log"
echo '[M173 HOST PASS] typed PCI/MMIO resources, original VirtIO ports, previous ABI gates, independent dynamic GUI socket'
