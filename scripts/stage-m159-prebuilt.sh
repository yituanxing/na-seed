#!/bin/sh
# M159: one signed Alpine APK overlay with 8 SVN + 5 NSS + 17 Netpbm ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m159-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M159 DUPLICATE ELF] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M159 HISTORICAL DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 30 ] || {
    echo "[M159 INVENTORY FAIL] require exactly 30 new native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m159-batch
work="build/external/m159-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in subversion nss-tools netpbm; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M159 APK FAIL] $port original Alpine $package not installed" >&2
        exit 1
    }
done
for input in m159-color.ppm m159-gray.pgm m159-bw.pbm \
             m159-multi.pnm m159-rgb.txt m159-svn-note.txt m159-test-cert.p12; do
    test -s "$root/tests/$input" || exit 1
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *)
        echo "[M159 ELF FAIL] path escaped signed guest root: $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M159 ELF FAIL] BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M159 ELF FAIL] not an ELF: $elf" >&2; exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m159-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m159-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 30 ] || {
    echo "[M159 ELF FAIL] only $unique distinct SHA256 / required 30" >&2
    exit 1
}
echo "[M159 APK STAGE PASS] $port 30 distinct signed native ELF SHA256 (SVN/NSS/image)"
echo '[M159 REAL APPLICATION EXECUTION PENDING] native three-ISA QEMU is a separate gate'
