#!/bin/sh
# Signed Alpine APK import and strict 36-independent-ELF admission.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m153-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M153 DUPLICATE PATH] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M153 HISTORIC DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 36 ] || {
    echo "[M153 STAGE FAIL] expected 36 distinct new paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m153-batch
work="build/external/m153-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in binutils gcc ccache clang21-extra-tools radare2 pahole moreutils \
               nasm yara bloaty cflow difftastic mold; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M153 STAGE FAIL] missing official signed APK $package" >&2
        exit 1
    }
done
for fixture in m153-native.o m153-native-v2.o m153-fixture.c \
               m153-fixture-v2.c m153-style.c m153-rule.yar \
               m153-assembly.asm m153-text.txt m153-text-v2.txt; do
    test -s "$root/tests/$fixture" || exit 1
done
root_canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$root_canonical"/*) ;; *)
        echo "[M153 STAGE FAIL] ELF escaped signed APK root: $elf" >&2; exit 1 ;; esac
    case "$resolved" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M153 STAGE FAIL] BusyBox alias: $elf" >&2; exit 1 ;; esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M153 STAGE FAIL] not an actual ELF: $elf" >&2; exit 1; }
    sha256sum "$resolved"
done < "$manifest" > "$work/m153-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m153-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 36 ] || {
    echo "[M153 STAGE FAIL] $port $unique unique SHA, require 36" >&2; exit 1; }
echo "[M153 SIGNED APK STAGE PASS] $port 36 distinct official original ELF SHA256"
echo '[M153 GUEST PENDING] QEMU functional acceptance required'
