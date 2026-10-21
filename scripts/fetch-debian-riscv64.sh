#!/bin/sh
# Real official Debian 13 native RV64 busybox-static, reproducibly pinned.
# Same original BusyBox upstream package family as the existing x86 tests.
set -eu
dir=build/port-riscv64/full
mkdir -p "$dir"/debian-pkg "$dir"/debian-extracted
name='busybox-static_1.37.0-6+b9_riscv64.deb'
sha='4add476d2b185c5b487c285b38d790f0881a7e4a8e2ddde835f917e770eb8633'
archive="$dir/debian-pkg/$name"
if [ ! -s "$archive" ]; then
    curl -fLsS --retry 3 -o "$archive.tmp" \
     "https://deb.debian.org/debian/pool/main/b/busybox/$name"
    mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' "$sha" "$archive" | sha256sum -c -
dpkg-deb -x "$archive" "$dir/debian-extracted"
source="$dir/debian-extracted/usr/bin/busybox"
test -s "$source"
cp "$source" "$dir/busybox"
readelf -h "$dir/busybox" | grep -q 'Machine:.*RISC-V'
readelf -h "$dir/busybox" | grep -q 'EXEC (Executable file)'
if readelf -l "$dir/busybox" | grep -q INTERP; then
  echo '[DEBIAN RV64] busybox-static unexpectedly has PT_INTERP' >&2
  exit 1
fi
echo '[DEBIAN RV64] verified original official static BusyBox ELF64'
