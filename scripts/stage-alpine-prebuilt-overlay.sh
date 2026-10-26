#!/bin/sh
# Shared host-only original signed APK overlay. No compiler or installation
# script is executed inside NaSeed guests. One staging owner for M58+.
set -eu
[ "$#" -eq 5 ] || {
    echo 'usage: stage-alpine-prebuilt-overlay.sh PORT PROFILE PKGS ELFS COPIES' >&2
    exit 2
}
port=$1
profile=$2
package_file=$3
elf_file=$4
copies_file=$5
case "$port" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) exit 2 ;;
esac
case "$profile" in m58-runtime|m59-worktree|m60-terminal|m65-observe|m66-monitor|m67-control|m69-inspect|m70-vmfd|m72-strace|m73-strace|m74-strace) ;; *) exit 2 ;; esac
for manifest in "$package_file" "$elf_file" "$copies_file"; do
    test -s "$manifest" || exit 1
done

NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh "$port"
base="build/external/m39-packages/$port"
work="build/external/$profile/$port"
mkdir -p "$work"
cp -a "$base/rootfs" "$work/rootfs"
root="$work/rootfs"
# Every Linux rootfs requires the parent device directory even when its
# device nodes are supplied virtually by the kernel, not host mknod.
mkdir -p "$root/dev"
target="$(pwd)/$root"
packages="$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$package_file")"
test -n "$packages"
if ! docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    alpine:3.23 sh -ec '
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
            --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
            add $APK_PACKAGES
    ' > "$work/apk-install.log" 2>&1; then
    echo "[ALPINE OVERLAY FAIL] $profile $port" >&2
    tail -90 "$work/apk-install.log" >&2
    exit 1
fi
sudo chown -R "$(id -u):$(id -g)" "$root"

while IFS= read -r program; do
    case "$program" in ''|'#'*) continue ;; esac
    case "$program" in /*|*../*|*/../*|*' '*) exit 2 ;; esac
    binary="$root/$program"
    test -f "$binary" && test -x "$binary" || {
        echo "[ALPINE OVERLAY FAIL] missing $port ELF $program" >&2
        exit 1
    }
    readelf -h "$binary" | grep -Fq "$machine" || {
        echo "[ALPINE OVERLAY FAIL] wrong ISA $port ELF $program" >&2
        exit 1
    }
done < "$elf_file"

tab="$(printf '\t')"
while IFS="$tab" read -r source destination; do
    case "$source" in ''|'#'*) continue ;; esac
    case "$destination" in tests/*) ;; *) exit 2 ;; esac
    case "$destination" in *../*|*/../*|*' '*) exit 2 ;; esac
    test -f "$source" || exit 1
    cp "$source" "$root/$destination"
done < "$copies_file"

(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || {
                echo "[ALPINE OVERLAY FAIL] CPIO path too long: $path" >&2
                exit 1
            }
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
printf '%s\n' "$packages" > "$work/requested-packages.txt"
echo "[ALPINE OVERLAY PASS] $port $profile original signed target ELF, host-installed"
