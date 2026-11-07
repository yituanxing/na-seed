#!/bin/sh
# 30 independent, previously unadmitted signed Netpbm ELFs on each native ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m165-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M165 DUPLICATE] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M165 ALREADY ADMITTED] $elf in $old" >&2; exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 30 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m165-batch
work="build/external/m165-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed"
for fixture in tests/m165-batch-run.sh tests/m163-color.ppm \
               tests/m163-gray.pgm tests/m163-bw.pbm tests/m163-rgb.txt; do
    test -s "$root/$fixture"
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox) exit 1 ;; esac
    test -x "$resolved" || exit 1
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    sha256sum "$resolved"
done < "$manifest" > "$work/m165-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m165-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 30 ] || exit 1
echo "[M165 SIGNED APK PASS] $port 30 new native image ELF SHA256"
