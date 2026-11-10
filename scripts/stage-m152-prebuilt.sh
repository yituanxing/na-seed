#!/bin/sh
# Stage exactly 35 new upstream Alpine 3.23 signed ELF identities on the HOST.
# The original APK binaries are neither rebuilt nor modified in the guest.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m152-original-elfs.txt
count=0
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  for old in scripts/m[0-9]*-original-elfs.txt; do
    [ "$old" = "$manifest" ] && continue
    if grep -Fxq "$elf" "$old"; then
      echo "[M152 HISTORICAL DUPLICATE] $elf in $old" >&2
      exit 1
    fi
  done
  count=$((count+1))
done < "$manifest"
[ "$count" -eq 35 ] || { echo "[M152 FAIL] expected 35, got $count" >&2; exit 1; }
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m152-batch
root="build/external/m152-batch/$1"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/lib/apk/db/installed"
abs=$(cd "$root/rootfs" && pwd -P)
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  resolved=$(readlink -f "$root/rootfs/$elf") || exit 1
  case "$resolved" in "$abs"/*) ;; *) exit 1 ;; esac
  test -f "$resolved" && test -x "$resolved"
  case "$resolved" in "$abs"/bin/busybox|"$abs"/usr/bin/busybox) exit 1 ;; esac
  sha256sum "$resolved"
done < "$manifest" > "$root/m152-original-elfs.sha256"
[ "$(awk '{print $1}' "$root/m152-original-elfs.sha256" | sort -u | wc -l)" -eq 35 ] || {
  echo '[M152 FAIL] symlink aliases and identical ELF SHA are not new programs' >&2
  exit 1
}
echo '[M152 STAGED] 35 original signed Alpine ELF identities; native QEMU pending'
