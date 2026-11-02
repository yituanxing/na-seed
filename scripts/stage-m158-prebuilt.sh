#!/bin/sh
# Thirty historically new Alpine Netpbm ELF codec identities in three ISAs.
# Signed APK installation, ISA verification and canonical CPIO have one owner.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m158-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M158 DUPLICATE ELF] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M158 HISTORICAL DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 30 ] || {
    echo "[M158 INVENTORY FAIL] need 30 native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m158-batch
work="build/external/m158-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq "P:netpbm" "$root/lib/apk/db/installed" || {
    echo "[M158 STAGE FAIL] no signed Netpbm APK" >&2
    exit 1
}
for fixture in m158-color.ppm m158-palette.ppm m158-gray.pgm m158-bw.pbm; do
    test -s "$root/tests/$fixture" || {
        echo "[M158 STAGE FAIL] missing image: $fixture" >&2
        exit 1
    }
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *)
        echo "[M158 STAGE FAIL] ELF escapes root: $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M158 STAGE FAIL] BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M158 STAGE FAIL] non-ELF original: $elf" >&2; exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m158-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m158-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 30 ] || {
    echo "[M158 STAGE FAIL] $port only $unique distinct ELF images" >&2
    exit 1
}
echo "[M158 APK STAGE PASS] $port 30 distinct original signed Alpine ELF hashes"
echo '[M158 GUEST SEMANTICS PENDING] native QEMU is the separate execution gate'
