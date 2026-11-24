#!/bin/sh
# M129: verify 15 genuinely distinct original signed procps/psmisc/util-linux ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# Historical ELF paths, not just the latest batch, belong to one census.
sh scripts/acceptance/check-elf-history.sh M129 scripts/m129-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m129-batch
work="build/external/m129-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in procps-ng psmisc util-linux-misc flock tmux; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M129 HOST STAGE FAIL] $port missing signed $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m129-original-elfs.txt "$work/m129-original-signed-elfs.sha256" 15 alias-path M129
echo "[M129 HOST STAGE PASS] $port 15 globally new signed process/monitoring ELFs"
