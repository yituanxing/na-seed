#!/bin/sh
# Reproducible extraction of official Debian 13 static binaries.
set -eu
kind="$1"
out="$2"
case "$kind" in
bash)
  filename='bash-static_5.2.37-2+b10_amd64.deb'
  sha='a8dfc61a50e70cdf2b790dcd9e1c45e2e42f396988e7fed1c45b1444df932953'
  pool='bash'
  binary='bash-static'
  ;;
busybox)
  filename='busybox-static_1.37.0-6+b9_amd64.deb'
  sha='598a3fd92bdafc34cd81196b2952ad36e910e47a4ecf162f8ce5e8e262598e53'
  pool='busybox'
  binary='busybox'
  ;;
*)
  echo "Usage: $0 bash|busybox ABSOLUTE_BUILD_DIR" >&2
  exit 2
  ;;
esac
dir="$out/debian"
mkdir -p "$dir/packages" "$dir/files" "$out/userspace"
archive="$dir/packages/$filename"
if [ ! -f "$archive" ]; then
  curl -fLsS --retry 3 -o "$archive.tmp" \
    "https://deb.debian.org/debian/pool/main/b/$pool/$filename"
  mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' "$sha" "$archive" | sha256sum -c -
root="$dir/files/$kind"
mkdir -p "$root"
dpkg-deb -x "$archive" "$root"
# Use the package's known executable path, not a same-named text helper.
source="$root/usr/bin/$binary"
if [ -z "$source" ] || [ ! -s "$source" ]; then
  echo "[DEBIAN] binary $binary not found in $filename" >&2
  exit 1
fi
target="$out/userspace/debian-$kind"
cp "$source" "$target"
chmod +x "$target"
readelf -h "$target" | grep -q 'EXEC (Executable file)'
if readelf -l "$target" | grep -q INTERP; then
  echo "[DEBIAN] dynamic interpreter unexpected" >&2
  exit 1
fi
if [ "$kind" = bash ]; then
  test "$("$target" --noprofile --norc -c 'echo hello')" = hello
else
  test "$("$target" echo hello)" = hello
fi
echo "[DEBIAN] verified unmodified $filename, static ELF, host stdout=hello"
