#!/bin/sh
# The SAME Debian busybox-static source package/version across ISA ports.
# Only ELF machine and byte checksum differ. Caller chooses the artifact path.
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: fetch-busybox-fixture.sh amd64|riscv64 OUTPUT_FILE' >&2
    exit 2
fi
arch=$1
target=$2
manifest=scripts/busybox-static-fixtures.txt
test -s "$manifest"
matches=$(awk -F '|' -v arch="$arch" '$1 == arch { print }' "$manifest")
case "$matches" in
    ''|*'
'*) echo "[BUSYBOX FIXTURE] missing/duplicate ISA $arch" >&2; exit 2 ;;
esac
IFS='|' read -r found version upstream checksum machine <<EOF
$matches
EOF
[ "$found" = "$arch" ] && [ "$upstream" = '1.37.0' ]
base=busybox-static_${version}_${arch}.deb
mkdir -p "$(dirname "$target")"
debpkg="$(dirname "$target")/.busybox-pkg/$base"
root="$(dirname "$target")/.busybox-extract/$arch"
mkdir -p "$(dirname "$debpkg")" "$root"
if [ ! -s "$debpkg" ]; then
    curl -fLsS --retry 3 -o "$debpkg.tmp" \
      "https://deb.debian.org/debian/pool/main/b/busybox/$base"
    mv "$debpkg.tmp" "$debpkg"
fi
printf '%s  %s\n' "$checksum" "$debpkg" | sha256sum -c -
dpkg-deb -x "$debpkg" "$root"
source="$root/usr/bin/busybox"
test -s "$source"
readelf -h "$source" | grep -Fq "$machine"
readelf -h "$source" | grep -q 'EXEC (Executable file)'
if readelf -l "$source" | grep -q INTERP; then
    echo '[BUSYBOX FIXTURE] static BusyBox unexpectedly has PT_INTERP' >&2
    exit 1
fi
cp "$source" "$target"
chmod +x "$target"
printf '%s\n' "debian_source=busybox upstream=$upstream package=$version arch=$arch sha256=$checksum" > "$target.fixture"
echo "[BUSYBOX FIXTURE] verified Debian $upstream/$version $arch static ELF"
