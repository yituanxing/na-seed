#!/bin/sh
# M203 changes only two original Alpine configuration files on a disposable
# M202 signed ext4 copy: enable authentic BusyBox getty/login on VT1.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-m202-alpine-interactive-fbi.sh "$port"
source="$PWD/build/external/m202-alpine-interactive-fbi/$port"
work="$PWD/build/external/m203-alpine-fbcon/$port"
mkdir -p "$work"
disk="$work/m203-original-alpine-console.img"
rm -f "$disk"
python3 scripts/m203-disk-fixture.py \
    "$source/m202-original-interactive-fbi.img" "$disk"
cp "$source/m202-original-signed-fbi.sha256" \
   "$work/m203-original-signed-fbi.sha256"
cp "$source/m202-official-installed-apk-db" \
   "$work/m203-official-installed-apk-db"
test -s "$disk" && test -s "$work/m203-original-signed-fbi.sha256"
echo "[M203 ALPINE VT GETTY ROOT PASS] $port authenticated original Alpine original-ELF login on real graphical tty1"
