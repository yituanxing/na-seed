#!/bin/sh
# Boot ORIGINAL Alpine OpenRC PID1, getty and BusyBox poweroff on native ISAs.
# Packages are fetched through apk's standard signature verification.
set -eu
[ "$#" -eq 1 ] || { echo "usage: native-openrc.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
base="build/native-boot/$arch"
root="$base/openrc-root"
test -s "$base/alpine-root/bin/busybox"
sudo rm -rf "$root"
mkdir -p "$root"
cp -a "$base/alpine-root/." "$root/"
docker run --rm -v "$PWD/$root:/target" alpine:3.23 sh -ec "
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch $arch --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \\
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \\
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \\
        add alpine-base openrc
"
test -s "$root/sbin/init" || test -L "$root/sbin/init"
test -e "$root/etc/inittab"
sudo sed -i '/^tty[1-6]:/d; /^ttyS0:/d' "$root/etc/inittab"
echo 'ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100' |
    sudo tee -a "$root/etc/inittab" >/dev/null
if ! sudo grep -qx ttyS0 "$root/etc/securetty" 2>/dev/null; then
    echo ttyS0 | sudo tee -a "$root/etc/securetty" >/dev/null
fi
password_hash=$(openssl passwd -6 -salt naseed-ci-only naseed-ci-only)
sudo python3 - "$root/etc/shadow" "$password_hash" <<'PY'
import pathlib
import sys
path, password_hash = pathlib.Path(sys.argv[1]), sys.argv[2]
lines = path.read_text().splitlines()
found = False
for i, line in enumerate(lines):
    parts = line.split(":")
    if parts[0] == "root":
        parts[1] = password_hash
        lines[i] = ":".join(parts)
        found = True
assert found, "real root account missing"
path.write_text("\n".join(lines) + "\n")
PY
disk="$base/openrc.ext4"
truncate -s 512M "$disk"
sudo mkfs.ext4 -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$root" "$disk"
e2fsck -fn "$disk"
cp "$base/efi.img" "$base/openrc-efi.img"
mcopy -o -i "$base/openrc-efi.img" \
    "build/port-$arch/full/kernel.elf" ::/boot/kernel
mcopy -i "$base/openrc-efi.img" "$base/alpine.cpio" ::/boot/alpine.cpio
printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.argv=/sbin/init\n    module_path: boot():/boot/alpine.cpio\n    dtb_path: boot():/boot/qemu-virt.dtb\n' > "$base/openrc-limine.conf"
mcopy -o -i "$base/openrc-efi.img" "$base/openrc-limine.conf" \
    "::/boot/limine/limine.conf"
python3 tests/native-boot.py "$arch" "$base/openrc-efi.img" \
    "$base/openrc-serial.log" init "$disk"
e2fsck -fn "$disk"
echo "PASS: $arch original Alpine OpenRC /sbin/init -> UART getty -> root login -> clean poweroff"
