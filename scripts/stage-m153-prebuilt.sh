#!/bin/sh
# Signed Alpine APK import and strict 36-independent-ELF admission.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m153-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M153 "$manifest" --count --distinct)
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
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m153-original-signed-elfs.sha256" 36 contained-elf M153
echo "[M153 SIGNED APK STAGE PASS] $port 36 distinct official original ELF SHA256"
echo '[M153 GUEST PENDING] QEMU functional acceptance required'
