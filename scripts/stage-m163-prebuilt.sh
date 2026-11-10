#!/bin/sh
# A complete NEW 33-ELF native image suite on each of x86_64/RV64/ARM64.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m163-original-elfs.txt
seen=' '
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M163 DUPLICATE] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M163 ALREADY ADMITTED] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 33 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m163-batch
work="build/external/m163-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed"
for fixture in tests/m163-batch-run.sh tests/m163-color.ppm \
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
done < "$manifest" > "$work/m163-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m163-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 33 ] || exit 1
echo "[M163 SIGNED APK PASS] $port 33 previously unseen native Netpbm ELF SHA256"
