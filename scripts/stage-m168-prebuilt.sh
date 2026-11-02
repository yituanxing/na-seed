#!/bin/sh
# Alpine v3.23 signed binaries, with new lynx and mc disjoint from history.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
manifest=scripts/m168-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for previous in scripts/m[0-9]*-original-elfs.txt; do
        [ "$previous" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$previous"; then
            echo "[M168 STAGE FAIL] historically counted $elf" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 2 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m168-batch
work="build/external/m168-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
test -s "$root/tests/m167-desktop-fixture.ppm"
test -s "$root/tests/m168-home.html"
for pkg in lynx mc dialog links w3m imagemagick evtest libevdev-tools fbgrab; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed"
done
for app in naseed-desktop naseed-image naseed-terminal naseed-browser; do
    test -x "$root/usr/bin/$app" || exit 1
    readelf -h "$root/usr/bin/$app" | grep -Fq "$machine" || exit 1
    if readelf -l "$root/usr/bin/$app" | grep -Fq INTERP; then exit 1; fi
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    test -x "$resolved" || exit 1
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$resolved" | grep -Fq "$machine" || exit 1
    sha256sum "$resolved"
done < "$manifest" > "$work/m168-original-signed-elfs.sha256"
[ "$(awk '{print $1}' "$work/m168-original-signed-elfs.sha256" | sort -u | wc -l)" -eq 2 ] || exit 1
echo "[M168 STAGE PASS] $port two new original signed Alpine browser/filemanager ELFs"
