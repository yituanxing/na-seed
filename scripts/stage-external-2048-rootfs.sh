#!/bin/sh
# Entire guest userland: *real* original game ELF + normal Linux PID1.
# No BusyBox or program-specific kernel substitutions.
set -eu
work=build/external/2048/amd64
root="$work/rootfs"
test -s "$work/2048"
mkdir -p "$root/bin" "$root/tmp"
cp "$work/2048" "$root/bin/2048"
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/2048-native-init.c -o "$root/bin/game-init"
chmod 755 "$root" "$root/bin" "$root/bin/2048" "$root/bin/game-init"
chmod 1777 "$root/tmp"
touch -t 197001010000 "$root" "$root/bin" "$root/tmp" \
    "$root/bin/2048" "$root/bin/game-init"
(
    cd "$root"
    printf '%s\n' . bin bin/2048 bin/game-init tmp |
      cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo '[GAME ROOTFS] unmodified original game and real C PID1 only'
