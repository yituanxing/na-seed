#!/bin/sh
# M172 focused host gate: one production device-node registry and the real
# display/FileOps/input adapters. Native hardware acceptance is deliberately
# separate: run the M171 QMP matrix once at the final commit.
set -eu
mkdir -p build/m172-device-display

cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I kernel \
  scripts/test-m172-device-publication.c kernel/drivers/core/chrdev.c \
  -o build/m172-device-display/node-publication
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/node-publication

# Preserve exact Linux fbdev UAPI, byte bounds and positioned FileOps.
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I kernel \
  scripts/test-fbdev-core.c kernel/drivers/display/fbdev.c \
  -o build/m172-device-display/fbdev-core
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/fbdev-core
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-builtin \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I kernel \
  kernel/drivers/display/fbdev.c kernel/drivers/display/fbdev_test.c \
  -o build/m172-device-display/registered-display
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/registered-display boot
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/registered-display provider
# M186: class owner detaches only after live OpenFile consumers release;
# an explicit vacancy permits post-lock rebind without reopening bootfb's
# one-shot takeover or silently allowing first-time late registrations.
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/registered-display late
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/registered-display detach
ASAN_OPTIONS=detect_leaks=0 build/m172-device-display/registered-display boot-rebind
sh scripts/check-m186-fbdev-node-pins.sh
sh scripts/check-m167-fb-pwrite.sh

# The production VFS-first bootstrap fallback, OpenFile final-close, TTY,
# PTY and socket descriptor paths all share the new node lease.  Keep the
# historical real syscall fixture honest after moving device publication.
sh scripts/check-m106-usercopy-syscalls.sh
sh scripts/check-lwip-socket-abi.sh

# The input driver, evdev and character device adapters remain one shared
# implementation for native x86-64, RV64 and ARM64, including ISA syntax.
sh scripts/check-m166-input-port.sh
echo '[M172 HOST PASS] device publication, pinned node ownership, fbdev, FileOps and input/evdev'
