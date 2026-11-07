#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
count=0
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  for old in scripts/m[0-9]*-original-elfs.txt; do
    [ "$old" = scripts/m144-original-elfs.txt ] && continue
    if grep -Fxq "$elf" "$old"; then
      echo "[M144 DUPLICATE] $elf in $old" >&2; exit 1
    fi
  done
  count=$((count+1))
done < scripts/m144-original-elfs.txt
[ "$count" -eq 5 ]
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m144-batch
root=build/external/m144-batch/$1
test -s "$root/rootfs.cpio"
abs=$(cd "$root/rootfs" && pwd -P)
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  real=$(readlink -f "$root/rootfs/$elf")
  case "$real" in "$abs"/*) ;; *) exit 1 ;; esac
  test -f "$real" && test -x "$real"
  case "$real" in "$abs"/bin/busybox|"$abs"/usr/bin/busybox) exit 1 ;; esac
  sha256sum "$real"
done < scripts/m144-original-elfs.txt > "$root/m144-original-elfs.sha256"
test "$(awk '{print $1}' "$root/m144-original-elfs.sha256" | sort -u | wc -l)" -eq 5
echo '[M144 STAGED] five different original signed Alpine executables'
