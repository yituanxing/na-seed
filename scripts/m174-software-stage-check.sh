#!/bin/sh
# The shared overlay checks Alpine signature keys and native ELF machine IDs.
# This stage additionally fails closed for non-ELF aliases and SHA duplicates.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
work="build/external/m174-software-batch/$port"
root="$work/rootfs"
manifest=scripts/m174-software-original-elfs.txt
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in fbgrab imagemagick evtest libevdev-tools dialog links w3m \
               lynx mc dbus inotify-tools sysstat tig; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M174 SOFTWARE STAGE FAIL] $port missing signed APK: $package" >&2
        exit 1
    }
done
for fixture in tests/m167-desktop-fixture.ppm tests/m168-home.html \
               tests/m174-software-run.sh; do
    test -s "$root/$fixture" || {
        echo "[M174 SOFTWARE STAGE FAIL] $port fixture missing: $fixture" >&2
        exit 1
    }
done
cmp -s "$manifest" "$root/tests/m174-software-original-elfs.txt" || {
    echo "[M174 SOFTWARE STAGE FAIL] $port guest ELF list differs from source" >&2
    exit 1
}
canonical=$(cd "$root" && pwd -P)
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$canonical"/*) ;;
        *) echo "[M174 SOFTWARE STAGE FAIL] $port escaped root: $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in
        "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
            echo "[M174 SOFTWARE STAGE FAIL] $port BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || {
        echo "[M174 SOFTWARE STAGE FAIL] $port non-ELF: $elf" >&2; exit 1
    }
    readelf -h "$resolved" | grep -Fq "$machine" || {
        echo "[M174 SOFTWARE STAGE FAIL] $port wrong ISA: $elf" >&2; exit 1
    }
    sha256sum "$resolved"
    count=$((count+1))
done < "$manifest" > "$work/m174-software-original-signed-elfs.sha256"
[ "$count" -eq 20 ] || exit 1
unique=$(awk '{print $1}' "$work/m174-software-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 20 ] || {
    echo "[M174 SOFTWARE STAGE FAIL] $port only $unique/20 distinct actual ELF SHA256" >&2
    exit 1
}
echo "[M174 SOFTWARE SIGNED ELF PASS] $port 20 original Alpine ELF SHA256; QEMU guest semantics pending"
