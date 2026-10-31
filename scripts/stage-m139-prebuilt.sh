#!/bin/sh
# M139: twenty original signed font, image, process, crypto ELF programs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Historical ELF paths are append-only; neither earlier nor later batches
# may silently count one M139 executable as a newly ported target.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m139-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M139 DUPLICATE ORIGINAL ELF] $elf previously in $old" >&2
            exit 1
        fi
    done
done < scripts/m139-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m139-batch
work="build/external/m139-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
root_canonical=$(cd "$root" && pwd -P)
for pkg in fontconfig font-dejavu netpbm util-linux-misc mcookie \
           gnutls-utils gnupg-gpgconf; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M139 HOST STAGE FAIL] $port missing signed $pkg" >&2
        exit 1
    }
done

count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root_canonical"/*) ;;
        *) echo "[M139 HOST STAGE FAIL] signed ELF resolves outside guest root: $elf -> $target" >&2
           exit 1 ;;
    esac
    case "$target" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M139 HOST STAGE FAIL] BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -x "$target" || {
        echo "[M139 HOST STAGE FAIL] not executable: $elf" >&2
        exit 1
    }
    sha256sum "$target"
    count=$((count + 1))
done < scripts/m139-original-elfs.txt > "$work/m139-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m139-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 20 ] || [ "$unique" -ne 20 ]; then
    echo "[M139 HOST STAGE FAIL] $port only $count paths/$unique distinct SHA256" >&2
    exit 1
fi
echo "[M139 HOST STAGE PASS] $port 20 new distinct signed original ELFs"
