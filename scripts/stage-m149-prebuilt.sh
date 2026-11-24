#!/bin/sh
# Host-only signed Alpine 3.23 APK acquisition and 35 unique native ELF checks.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m149-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M149 "$manifest" --count)
[ "$count" -eq 35 ] || { echo "[M149 FAIL] 35 required, found $count" >&2; exit 1; }
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m149-batch
root="build/external/m149-batch/$1"
test -s "$root/rootfs.cpio" && test -s "$root/rootfs/lib/apk/db/installed"
abs=$(cd "$root/rootfs" && pwd -P)
while IFS= read -r elf; do
  case "$elf" in ''|'#'*) continue ;; esac
  resolved=$(readlink -f "$root/rootfs/$elf") || exit 1
  case "$resolved" in "$abs"/*) ;; *) exit 1 ;; esac
  test -f "$resolved" && test -x "$resolved"
  case "$resolved" in "$abs"/bin/busybox|"$abs"/usr/bin/busybox) exit 1 ;; esac
  sha256sum "$resolved"
done < "$manifest" > "$root/m149-original-elfs.sha256"
[ "$(awk '{print $1}' "$root/m149-original-elfs.sha256" | sort -u | wc -l)" -eq 35 ] || {
  echo '[M149 FAIL] identical ELF hashes cannot be counted as new identities' >&2; exit 1;
}
echo '[M149 STAGED] 35 new independently signed Alpine ELF identities; QEMU pending'
