#!/bin/sh
# M147 only admits independently signed Alpine APK binaries on the host.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m147-original-elfs.txt
count=0
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  for old in scripts/m[0-9]*-original-elfs.txt; do
    [ "$old" = "$manifest" ] && continue
    if grep -Fxq "$elf" "$old"; then
      echo "[M147 HISTORICAL DUPLICATE] $elf in $old" >&2; exit 1
    fi
  done
  count=$((count+1))
done < "$manifest"
[ "$count" -eq 29 ] || { echo "[M147 FAIL] inventory count=$count" >&2; exit 1; }
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m147-batch
root="build/external/m147-batch/$1"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/lib/apk/db/installed"
abs=$(cd "$root/rootfs" && pwd -P)
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  resolved=$(readlink -f "$root/rootfs/$elf") || exit 1
  case "$resolved" in "$abs"/*) ;; *) exit 1 ;; esac
  test -f "$resolved" && test -x "$resolved"
  case "$resolved" in "$abs"/bin/busybox|"$abs"/usr/bin/busybox) exit 1 ;; esac
  sha256sum "$resolved"
done < "$manifest" > "$root/m147-original-elfs.sha256"
[ "$(awk '{print $1}' "$root/m147-original-elfs.sha256" | sort -u | wc -l)" -eq 29 ] || {
  echo '[M147 FAIL] identical binary identities cannot be double-counted' >&2; exit 1;
}
echo '[M147 STAGED] 29 genuinely new signed Alpine ELF identities; QEMU pending'
