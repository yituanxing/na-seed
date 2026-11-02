#!/bin/sh
# One genuinely new signed original Alpine ELF (fbgrab) plus proven M146 magick.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
grep -Fxq 'usr/bin/fbgrab' scripts/m161-original-elfs.txt
grep -Fxq 'usr/bin/magick' scripts/m161-original-elfs.txt
if grep -Fxq 'usr/bin/fbgrab' scripts/m[0-9]*-original-elfs.txt; then
    # Need to test old manifests individually rather than grep own identity.
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m161-original-elfs.txt ] && continue
        if grep -Fxq 'usr/bin/fbgrab' "$old"; then
            echo "[M161 STAGE FAIL] fbgrab was previously admitted in $old" >&2
            exit 1
        fi
    done
fi
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m161-batch
work="build/external/m161-batch/$port"
root="$work/rootfs"
for program in fbgrab magick; do
    resolved=$(readlink -f "$root/usr/bin/$program")
    canonical=$(cd "$root" && pwd -P)
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    test -x "$resolved"
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || exit 1
    sha256sum "$resolved"
done > "$work/m161-signed-original-elfs.sha256"
test -s "$root/tests/m161-stripes.ppm"
test -s "$root/tests/m161-batch-run.sh"
grep -Fxq 'P:fbgrab' "$root/lib/apk/db/installed"
grep -Fxq 'P:imagemagick' "$root/lib/apk/db/installed"
echo "[M161 SIGNED STAGE PASS] $port original fbgrab + ImageMagick"
