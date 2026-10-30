#!/bin/sh
# The same signed Alpine Git/dependency installation used by M51, without
# extra vendor patches or an independently hand-built executable.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m85-git.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
NAS_ALPINE_PROFILE=m85-git sh rootfs/alpine/build.sh "$port"
work="build/external/m85-git/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/tests/m85-git-run.sh"
test -s "$root/lib/apk/db/installed"
awk '$0=="P:git" {found=1} END {exit !found}' "$root/lib/apk/db/installed" || {
    echo "[M85 FAIL] $port original signed Alpine Git not installed" >&2
    exit 1
}
sha256sum "$root/usr/bin/git" > "$work/m85-signed-git-elf.sha256"
echo "[M85 STAGE PASS] $port original signed Alpine Git and APK dependency closure"
