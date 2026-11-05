#!/bin/sh
# Reuse exactly M201's signed Alpine APK and original fbi root, then add one
# *input PNG*, never a helper ELF or fake test renderer.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-m201-alpine-fbi.sh "$port"
work="$PWD/build/external/m202-alpine-interactive-fbi/$port"
source="$PWD/build/external/m201-alpine-fbi/$port"
mkdir -p "$work"
fixture="$work/m202-second-bars.png"
disk="$work/m202-original-interactive-fbi.img"
rm -f "$disk"
python3 scripts/m202-image-fixture.py "$fixture"
python3 scripts/m202-disk-fixture.py \
    "$source/m201-original-fbi.img" "$disk" "$fixture"
test -s "$disk" && test -s "$source/m201-original-fbi.sha256"
cp "$source/m201-original-fbi.sha256" "$work/m202-original-signed-fbi.sha256"
cp "$source/m201-official-installed-apk-db" "$work/m202-official-installed-apk-db"
echo "[M202 SIGNED ORIGINAL ALPINE ROOT PASS] $port unmodified upstream fbi, two different PNG inputs, one physical original ext4 boot"
