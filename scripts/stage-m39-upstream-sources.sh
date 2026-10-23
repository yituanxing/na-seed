#!/bin/sh
# Extend the unmodified Alpine GCC/musl sysroot with READ-ONLY upstream
# source. All compilation, link, ar and tests happen later INSIDE NaSeed.
set -eu
[ "$#" -eq 1 ] || exit 2
root=$1
sh scripts/fetch-m39-upstream-sources.sh
mkdir -p "$root/tests/src/zlib" "$root/tests/src/lua"
zlib=build/deps/m36-zlib/upstream
lua=build/deps/m39-upstream/lua-5.4.8
# Copy only git-tracked upstream blobs, not the host git database.
( cd "$zlib" && git ls-files -z | tar --null -T - -cf - ) |
    ( cd "$root/tests/src/zlib" && tar -xf - )
# Untouched official tarball contents; the upstream Makefiles are kept intact.
cp -a "$lua/." "$root/tests/src/lua/"
cp tests/lua-vm.lua "$root/tests/lua-vm.lua"
cp tests/lua-io.lua "$root/tests/lua-io.lua"
cp tests/m39-native-build.sh "$root/tests/build-m39.sh"
# Foreign-ISA apk --no-scripts omits the BusyBox post-install symlink
# trigger. Recover only the normal BusyBox applet layout from the same
# official Alpine 3.23 package series, never substitute its ELF contents.
# Installed full programs take precedence over applets (GNU make/ar/ld/gcc).
docker run --rm alpine:3.23 busybox --list-full |
while IFS= read -r applet; do
    case "$applet" in
        bin/*) relative=busybox ;;
        sbin/*) relative=../bin/busybox ;;
        usr/bin/*|usr/sbin/*) relative=../../bin/busybox ;;
        *) continue ;;
    esac
    target="$root/$applet"
    if [ -e "$target" ] || [ -L "$target" ]; then continue; fi
    mkdir -p "$(dirname "$target")"
    ln -s "$relative" "$target"
done
test -x "$root/usr/bin/make" && test -x "$root/usr/bin/gcc"
test -e "$root/usr/bin/ar" && test -s "$root/tests/src/zlib/configure"
test -s "$root/tests/src/lua/src/Makefile"
echo '[M39 ROOTFS] upstream untouched zlib/Lua, real GNU Make/GCC/ar, original BusyBox applets'
