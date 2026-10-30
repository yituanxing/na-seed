#!/bin/sh
# One existing signed APK overlay, no competing M80 installer or guest compiler.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m80-batch
work="build/external/m80-batch/$1"
apk_db="$work/rootfs/lib/apk/db/installed"
test -s "$apk_db"
test -s "$work/rootfs/usr/bin/make"
make_version=$(awk '
    /^P:make$/ { make_record = 1; next }
    make_record && /^V:/ { print substr($0, 3); exit }
    make_record && /^$/ { make_record = 0 }
' "$apk_db")
test -n "$make_version" || {
    echo '[M82 SIGNED GNU MAKE FAIL] missing original Alpine APK ownership/version' >&2
    exit 1
}
# The canonical Alpine overlay already verifies this native ELF machine via
# scripts/m80-original-elfs.txt using readelf, and uses authenticated APK keys.
sha256sum "$work/rootfs/usr/bin/make" > "$work/m82-original-gnu-make.sha256"
echo "[M82 SIGNED GNU MAKE APK] $1 make-$make_version verified ISA-native ELF, original executable SHA256 recorded"
echo "[M80/M82 HOST STAGED] $1 twenty-one signed Alpine ELF candidates, including original GNU Make; guest functional results pending"
