#!/bin/sh
# Stage a disposable signed Alpine root + real original fbida-fbi binary.
# No M197 guest-side APK WAN transaction, no M191 preliminary QEMU boot.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m201-alpine-fbi.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) apk_arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) apk_arch=riscv64; machine='RISC-V' ;;
    arm64) apk_arch=aarch64; machine='AArch64' ;;
    *) echo "[M201 STAGE FAIL] unsupported port: $port" >&2; exit 2 ;;
esac
for app in docker sudo readelf sha256sum cpio python3; do
    command -v "$app" >/dev/null || { echo "[M201 STAGE FAIL] missing $app" >&2; exit 1; }
done
work="$PWD/build/external/m201-alpine-fbi/$port"
base="$PWD/build/external/m191-alpine-rootfs/$port"
mkdir -p "$work" "$base/smoke/elf"
root="$base/rootfs"
baseline="$base/alpine-root.img"
if [ "${NAS_M201_REUSE_SIGNED_ROOT:-0}" = 1 ]; then
    test -d "$root" && test -s "$baseline" || {
        echo '[M201 STAGE FAIL] explicit reuse requested without full M191 original root' >&2
        exit 1
    }
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
else
    # Host-only existing checked official Alpine builder/partition proof.
    # Deliberately NOT probe-m191-alpine-root.sh: no extra original BusyBox VM.
    sh scripts/m191-stage-alpine-rootfs.sh "$port"
fi
test -s "$root/etc/alpine-release"
test "$(sudo cat "$root/etc/alpine-release")" = 3.23.7

# A rescue CPIO is required by M191's real physical-ext4 root selection,
# even when genuine /sbin/init on ext4 is the actual PID1. Export only the
# package-proven original Alpine BusyBox and interpreter, never a substitute.
sudo python3 scripts/m191-export-alpine-elf.py "$port" "$root" "$base/smoke/elf"
busybox="$base/smoke/elf/busybox"
loader="$base/smoke/elf/ld-musl-$apk_arch.so.1"
test -s "$busybox" && test -s "$loader"
readelf -h "$busybox" | grep -Fq "$machine"
readelf -l "$busybox" | grep -Fq "[Requesting program interpreter: /lib/ld-musl-$apk_arch.so.1]"
sha256sum "$busybox" "$loader" > "$work/m201-original-alpine-rescue.sha256"
sh scripts/stage-alpine-native-rootfs.sh "$busybox" "$loader" "$base/smoke/rescue"
rescue="$base/smoke/rescue/rootfs.cpio"
test -s "$rescue"

overlay=$(mktemp -d "$work/official-overlay.XXXXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"

# M198's established signed foreign-ISA Alpine package install, with the
# original v3.23 community source added for fbida-fbi. No --initdb,
# --allow-untrusted, relocated executable, LD_PRELOAD or binary patching.
apklog="$work/m201-original-signed-apk.log"
if ! docker run --rm --volume "$overlay:/target" \
    --env APK_ARCH="$apk_arch" alpine:3.23 sh -ec '
    test -n "$(find /target/etc/apk/keys -name "*.pub" -print -quit)"
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
        --no-scripts \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
        add fbida-fbi font-dejavu
' > "$apklog" 2>&1; then
    echo "[M201 SIGNED APK FAIL] official fbida-fbi/$apk_arch" >&2
    tail -100 "$apklog" >&2
    exit 1
fi

fbi="$overlay/usr/bin/fbi"
sudo test -x "$fbi"
readelf -h "$fbi" | grep -Fq "$machine"
readelf -h "$fbi" | grep -Eq 'Type:[[:space:]]+DYN'
readelf -l "$fbi" | grep -Fq "[Requesting program interpreter: /lib/ld-musl-$apk_arch.so.1]"
sudo grep -Fq 'P:fbida-fbi' "$overlay/lib/apk/db/installed"
sudo grep -Fq 'V:2.14-r6' "$overlay/lib/apk/db/installed"
sudo grep -Fq 'P:font-dejavu' "$overlay/lib/apk/db/installed"
font=$(sudo find "$overlay/usr/share/fonts" -type f -iname '*DejaVuSans*.ttf' -print -quit)
test -n "$font" && sudo test -s "$font" || {
    echo '[M201 FONT FAIL] signed Alpine font-dejavu lacks DejaVuSans TTF' >&2
    exit 1
}
sudo sha256sum "$fbi" > "$work/m201-original-fbi.sha256"
sudo cp "$overlay/lib/apk/db/installed" "$work/m201-official-installed-apk-db"
sudo chown "$(id -u):$(id -g)" "$work/m201-original-fbi.sha256" "$work/m201-official-installed-apk-db"
echo "[M201 ORIGINAL SIGNED APK PASS] $port $apk_arch fbida-fbi 2.14-r6, font-dejavu, PT_INTERP, APK DB, ELF SHA256"

fixture="$work/m201-four-bars.png"
python3 scripts/m201-image-fixture.py "$fixture"
sudo install -m 0644 "$fixture" "$overlay/root/m201-four-bars.png"
sudo test -s "$overlay/root/m201-four-bars.png"
sudo test ! -e "$overlay/root/m201-fbi.log"
sudo test ! -e "$overlay/root/m201-result.txt"

disk="$work/m201-original-fbi.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"
test -s "$disk"
printf '%s\n' "$disk" > "$work/m201-disk.path"
printf '%s\n' "$rescue" > "$work/m201-rescue.path"
printf '%s\n' "$busybox" > "$work/m201-busybox.path"
echo "[M201 HOST STAGE PASS] $port single signed original fbi, true Alpine 3.23.7 ext4 root, 800x600 original PNG; no preliminary QEMU"
