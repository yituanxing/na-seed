#!/bin/sh
# PTY and physical UART share one pure VMIN/VTIME policy; prove its behavior
# against Linux itself, then exercise the REAL NaSeed FileOps/usercopy/PTY.
# Native x86/RV64/ARM QEMU blocked read remains a separate guest gate.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    scripts/test-m188-linux-pty-oracle.c -o "$test_dir/linux-pty-oracle"
timeout 10 "$test_dir/linux-pty-oracle"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    scripts/test-m205-sshpass-linux-pty.c -o "$test_dir/sshpass-linux-pty"
timeout 10 "$test_dir/sshpass-linux-pty"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -fno-builtin -Wl,--gc-sections \
    -I scripts/m89-test-include -I kernel \
    scripts/test-m188-pty-vectors-kernel.c kernel/fs/file_io.c \
    kernel/mm/usercopy.c kernel/random.c kernel/fs/eventfd.c kernel/pipe.c \
    kernel/lib/string.c kernel/drivers/pty.c kernel/drivers/tty_ldisc.c \
    kernel/drivers/core/chrdev.c kernel/drivers/core/misc.c \
    kernel/drivers/display/fbdev.c \
    -o "$test_dir/naseed-pty-vectors"
timeout 10 "$test_dir/naseed-pty-vectors"
sh scripts/test-m105-pty-isig.sh
sh scripts/check-a013-linux-tty-vectors.sh
sh scripts/check-m187-vt100.sh
echo '[M188 PTY CORRECTNESS HOST PASS] Linux oracle, NaSeed production readv, ISIG, TTY and M187 blocking compatibility'
