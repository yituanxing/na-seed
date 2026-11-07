#!/bin/sh
# Stage exact original signed Alpine binaries and the separate native desktop
# process. Host compilers never modify signed APK executables; no guest build.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
esac
manifest=scripts/m167-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*)
        echo "[M167 STAGE FAIL] repeated ELF $elf" >&2; exit 1 ;;
    esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M167 STAGE FAIL] previously counted $elf" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 3 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m167-batch
work="build/external/m167-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
for app in naseed-desktop naseed-image naseed-terminal; do
    test -x "$root/usr/bin/$app" || {
        echo "[M167 STAGE FAIL] missing separately executable $app" >&2
        exit 1
    }
    readelf -h "$root/usr/bin/$app" | grep -Fq "$machine" || exit 1
    if readelf -l "$root/usr/bin/$app" | grep -Fq INTERP; then
        echo "[M167 STAGE FAIL] $app unexpectedly dynamic" >&2
        exit 1
    fi
done
test -s "$root/tests/m167-desktop-fixture.ppm"
for package in evtest libevdev-tools fbgrab imagemagick dialog links w3m; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed"
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    test -x "$resolved" || exit 1
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        exit 1 ;;
    esac
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    readelf -h "$resolved" | grep -Fq "$machine" || exit 1
    sha256sum "$resolved"
done < "$manifest" > "$work/m167-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m167-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 3 ] || exit 1
echo "[M167 SIGNED STAGE PASS] $port three new independent original Alpine terminal ELFs"
