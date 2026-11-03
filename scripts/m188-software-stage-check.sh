#!/bin/sh
# Signed same-CPIO native-ELF check. Vim/Nano are historical; only mg/rdfind
# can increase M174+M187's 46 unique, genuine original ELF SHA256 count.
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
installed="$root/lib/apk/db/installed"
manifest=scripts/m188-original-elfs.txt
test -s "$work/rootfs.cpio" && test -s "$installed"
for package in vim nano mg rdfind; do
    grep -Fxq "P:$package" "$installed" || {
        echo "[M188 SOFTWARE STAGE FAIL] $port missing signed Alpine APK: $package" >&2
        exit 1
    }
done
for fixture in tests/m188-editor-check.sh tests/m188-software-entry.sh \
               tests/m188-original-elfs.txt; do
    test -s "$root/$fixture" || {
        echo "[M188 SOFTWARE STAGE FAIL] $port same-CPIO fixture absent: $fixture" >&2
        exit 1
    }
done
cmp -s "$manifest" "$root/tests/m188-original-elfs.txt" || {
    echo "[M188 SOFTWARE STAGE FAIL] $port copied ELF manifest differs" >&2
    exit 1
}
for fixture in tests/m188-editor-check.sh tests/m188-software-entry.sh; do
    cmp -s "$fixture" "$root/$fixture" || {
        echo "[M188 SOFTWARE STAGE FAIL] $port fixture differs: $fixture" >&2
        exit 1
    }
done
canonical=$(cd "$root" && pwd -P)
for elf in usr/bin/vim usr/bin/nano; do
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox) exit 1 ;; esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$resolved" | grep -Fq "$machine" || exit 1
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$canonical"/*) ;;
        *) echo "[M188 SOFTWARE STAGE FAIL] $port escaped root: $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox) exit 1 ;; esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$resolved" | grep -Fq "$machine" || exit 1
    sha256sum "$resolved"
    count=$((count+1))
done < "$manifest" > "$work/m188-software-new-signed-elfs.sha256"
[ "$count" -eq 2 ] || exit 1
test -s "$work/m174-software-original-signed-elfs.sha256" || exit 1
test -s "$work/m187-software-new-signed-elfs.sha256" || exit 1
unique=$(awk '{print $1}' "$work/m174-software-original-signed-elfs.sha256" \
    "$work/m187-software-new-signed-elfs.sha256" \
    "$work/m188-software-new-signed-elfs.sha256" | LC_ALL=C sort -u | wc -l)
[ "$unique" -eq 48 ] || {
    echo "[M188 SOFTWARE STAGE FAIL] $port only $unique/48 independent original ELF SHA256" >&2
    exit 1
}
echo "[M188 SOFTWARE SIGNED ELF PASS] $port M79 Vim/M80 Nano reused; +2 native ELF mg/rdfind; 48 distinct M174+M187+M188; live Vim QEMU verification pending"
