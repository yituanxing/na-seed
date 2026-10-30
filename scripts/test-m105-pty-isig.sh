#!/bin/sh
# Host-only Linux PTY oracle for the exact C source intended for NaSeed guest.
# This cannot substitute for executing the same ELF in real system QEMU.
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
  tests/m105-pty-isig.c -o "$work/m105-pty-isig"
timeout 30 "$work/m105-pty-isig" --linux-pty-oracle
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
  -I kernel scripts/test-m105-pty-isig-kernel.c \
  kernel/drivers/pty.c kernel/drivers/tty_ldisc.c \
  -o "$work/m105-pty-isig-kernel"
timeout 10 "$work/m105-pty-isig-kernel"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
  -I kernel scripts/test-m105-tty-noflsh-kernel.c \
  kernel/drivers/tty.c kernel/drivers/tty_ldisc.c \
  -o "$work/m105-tty-noflsh-kernel"
timeout 10 "$work/m105-tty-noflsh-kernel"
echo '[M105 PTY ISIG HOST PASS] Linux oracle and genuine PTY/ldisc source unit tests'
