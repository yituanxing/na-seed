#!/bin/sh
# Deterministic original GNU nano + standard compiled Linux terminfo.
set -eu
work=build/external/nano/amd64
root="$work/rootfs"
test -s "$work/nano"
mkdir -p "$root/bin" "$root/tests" "$root/tmp" "$root/usr/share/terminfo/v"
cp "$work/nano" "$root/bin/nano"
gcc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie   tests/nano-native-init.c -o "$root/bin/nano-init"
cp tests/nano-note.txt "$root/tests/note.txt"
# Actual Linux ncurses terminfo database, no replacement fake terminfo API.
terminfo=
for path in /usr/share/terminfo/v/vt100 /lib/terminfo/v/vt100; do
  if [ -s "$path" ]; then terminfo=$path; break; fi
done
if [ -z "$terminfo" ]; then
  echo '[NANO] system vt100 terminfo missing (install ncurses-base)' >&2; exit 1
fi
cp "$terminfo" "$root/usr/share/terminfo/v/vt100"
chmod 755 "$root" "$root/bin" "$root/tests" "$root/usr" "$root/usr/share"   "$root/usr/share/terminfo" "$root/usr/share/terminfo/v" "$root/bin/nano" "$root/bin/nano-init"
chmod 1777 "$root/tmp"
chmod 644 "$root/tests/note.txt" "$root/usr/share/terminfo/v/vt100"
(
  cd "$root"
  printf '%s\n' . bin bin/nano bin/nano-init tests tests/note.txt tmp usr     usr/share usr/share/terminfo usr/share/terminfo/v usr/share/terminfo/v/vt100 |
    cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo '[NANO ROOTFS] original GNU nano + real Linux ncurses terminfo + launch PID1'
