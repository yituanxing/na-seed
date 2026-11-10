#!/bin/sh
# Focused M176 integration checks before the expensive THREE real QEMU boots.
# This gate does not emulate a successful hardware IRQ or network packet.
set -eu
python3 scripts/check-m176-net-irq-stats.py --selftest
python3 -m py_compile scripts/check-m176-net-irq-stats.py scripts/m176-http-server.py
sh -n scripts/probe-m176-net-irq.sh
sh -n scripts/stage-m176-irq-network.sh
sh -n scripts/stage-m174-devfs-software-gui.sh
mkdir -p build/m176-irq-host
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    scripts/test-m176-ncat-launch.c -o build/m176-irq-host/ncat-launch
file build/m176-irq-host/ncat-launch | grep -Eq 'ELF.*(static|statically linked)'
# Production IRQ/controller + Input regressions are the M175 baseline. The
# U-Boot net-port regression is already authoritative for original imported
# driver data-path and now extends to M176's IRQ attach/detach lifecycle.
sh scripts/check-m175-irq-core.sh
sh scripts/check-block-foundation.sh
echo '[M176 HOST IRQ NETWORK PASS] original U-Boot net port, deferred IRQ registration, signed Ncat launcher and strict guest stats oracle'
