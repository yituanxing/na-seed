#!/bin/sh
# Host-only signed APK import: 30 genuinely NEW native ELF identities.
# No substitution, compiling, or ABI emulation inside the NaSeed guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m154-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M154 HISTORICAL DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 30 ] || {
    echo "[M154 STAGE FAIL] 30 ELF paths expected; found $count" >&2; exit 1;
}
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m154-batch
work="build/external/m154-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in a2ps barcode chafa csview csvq enca enscript figlet \
               gnuplot ghostscript html2text jql libxml2-utils mandoc \
               pango-tools potrace psutils rsvg-convert taplo unrtf font-dejavu; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M154 STAGE FAIL] $port missing signed package $package" >&2
        exit 1
    }
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *)
        echo "[M154 STAGE FAIL] path escapes signed $port root: $elf" >&2
        exit 1 ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M154 STAGE FAIL] BusyBox alias $elf" >&2; exit 1 ;; esac
    test -f "$resolved" && test -x "$resolved"
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M154 STAGE FAIL] non-ELF $port $elf" >&2; exit 1;
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m154-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m154-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 30 ] || {
    echo "[M154 STAGE FAIL] $port 30 names but only $unique distinct SHA256" >&2
    exit 1
}
echo "[M154 SIGNED APK STAGE PASS] $port 30 unique original ELF SHA256"
echo '[M154 NATIVE QEMU PENDING] guest semantics still require actual acceptance'
