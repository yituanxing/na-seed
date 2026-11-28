#!/bin/sh
# Full original Alpine GUI on native ARM64/RV64: reuse the exact same
# Weston/OpenRC/Thunar desktop startup scripts as the x86-64 acceptance.
# Real signed Alpine packages, no custom Wayland/GTK fixture executables.
set -eu
[ "$#" -eq 1 ] || { echo "usage: native-desktop.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
base="build/native-boot/$arch"
root="$base/desktop-root"
test -s "$base/openrc-root/sbin/init" || test -L "$base/openrc-root/sbin/init"
sudo rm -rf "$root"
mkdir -p "$root"
sudo cp -a "$base/openrc-root/." "$root/"
docker run --rm -v "$PWD/$root:/target" alpine:3.23 sh -ec "
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch $arch --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \\
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \\
        -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \\
        add weston weston-backend-drm weston-shell-desktop weston-terminal \\
            libseat seatd eudev thunar mousepad dbus \\
            shared-mime-info xkeyboard-config adwaita-icon-theme font-dejavu \\
            python3 libdrm-tests
"
# Reuse the exact desktop supervisor/service from the x86-64 integration
# suite. Do not fork an ad-hoc native ISA launcher or substitute fake apps.
sudo python3 - "$root" <<'PY'
from pathlib import Path
import sys
source = Path("tests/alpine-root.sh").read_text()
root = Path(sys.argv[1])
entries = [
    ("DESKTOP_SESSION", "usr/local/sbin/naseed-desktop-session"),
    ("DESKTOP_SERVICE", "etc/init.d/naseed-desktop"),
    ("DRM_PROBE", "root/drm-probe.py"),
]
for marker, relative in entries:
    start_marker = "<<'" + marker + "'\n"
    start = source.index(start_marker) + len(start_marker)
    end = source.index("\n" + marker + "\n", start)
    dest = root / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(source[start:end] + "\n")
    dest.chmod(0o755)
PY
sudo mkdir -p "$root/etc/runlevels/default" "$root/root/desktop/documents"
sudo ln -s /etc/init.d/naseed-desktop "$root/etc/runlevels/default/naseed-desktop"
printf 'original native text\n' |
    sudo tee "$root/root/desktop/documents/note.txt" >/dev/null
sudo mkdir -p "$root/root/.config"
printf '[Default Applications]\ntext/plain=org.xfce.mousepad.desktop;\n' |
    sudo tee "$root/root/.config/mimeapps.list" >/dev/null
# XML/GSettings caches are architecture-neutral and must be regenerated
# after --no-scripts APK staging. Use actual Alpine host tools, not a guest
# syscall escape hatch.
docker run --rm -v "$PWD/$root:/target" alpine:3.23 sh -ec '
    apk add --no-cache glib gtk+3.0 shared-mime-info >/dev/null
    glib-compile-schemas /target/usr/share/glib-2.0/schemas
    update-mime-database /target/usr/share/mime
'
disk="$base/desktop.ext4"
truncate -s 1536M "$disk"
sudo mkfs.ext4 -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' -d "$root" "$disk"
e2fsck -fn "$disk"
cp "$base/efi.img" "$base/desktop-efi.img"
mcopy -o -i "$base/desktop-efi.img" \
    "build/port-$arch/full/kernel.elf" ::/boot/kernel
mcopy -i "$base/desktop-efi.img" "$base/alpine.cpio" ::/boot/alpine.cpio
printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.realtime=%s naseed.argv=/sbin/init\n    module_path: boot():/boot/alpine.cpio\n    dtb_path: boot():/boot/qemu-virt.dtb\n' "$(date -u +%s)" > "$base/desktop-limine.conf"
mcopy -o -i "$base/desktop-efi.img" "$base/desktop-limine.conf" \
    "::/boot/limine/limine.conf"
python3 tests/native-boot.py "$arch" "$base/desktop-efi.img" \
    "$base/desktop-serial.log" desktop "$disk"
e2fsck -fn "$disk"
echo "PASS: $arch original Alpine OpenRC -> Weston DRM -> Thunar GTK compositor and clean poweroff"
# Original GUI, no test-side file mutation: x86-like physical QMP input
# drives the actual guest's native MMIO VirtIO keyboard and mouse.
python3 tests/native-gui-roundtrip.py "$arch" edit
e2fsck -fn "$disk"
debugfs -R 'cat /root/desktop/documents/note.txt' "$disk" > "$base/gui-note.txt"
test "$(cat "$base/gui-note.txt")" = 'naseed gui roundtrip'
echo "PASS: $arch GUI Mousepad edited the actual ext4 disk; offline content confirmed"
# The second QEMU process/boot must independently navigate Thunar and reopen
# the saved file. No in-guest CLI performs the editing or opening.
python3 tests/native-gui-roundtrip.py "$arch" reopen
e2fsck -fn "$disk"
debugfs -R 'cat /root/desktop/documents/note.txt' "$disk" > "$base/gui-note.txt"
test "$(cat "$base/gui-note.txt")" = 'naseed gui roundtrip'
echo "PASS: $arch GUI persisted across independent QEMU boot; offline ext4 consistent"
# The same upstream Thunar input/clipboard/rename/delete semantics already
# passed on x86-64 Round 9. The actual MMIO QMP pointer calibration remains
# native, while the navigation/observation helpers are byte-equivalent.
python3 tests/native-gui-roundtrip.py "$arch" fileops
e2fsck -fn "$disk"
debugfs -R 'cat /root/desktop/archived.txt' "$disk" > "$base/gui-archive.txt"
test "$(cat "$base/gui-archive.txt")" = 'naseed gui roundtrip'
echo "PASS: $arch original Thunar multi-window copy rename permanent-delete; offline ext4 archive"
python3 tests/native-gui-roundtrip.py "$arch" fileops-reopen
e2fsck -fn "$disk"
debugfs -R 'cat /root/desktop/archived.txt' "$disk" > "$base/gui-archive.txt"
test "$(cat "$base/gui-archive.txt")" = 'naseed gui roundtrip'
echo "PASS: $arch Round 9 GUI reopened archive, deleted copy still absent after reboot"
# Deeper architecture-neutral graphics parity. Weston releases the real
# DRM master via OpenRC; upstream libdrm/GEM and modetest independently
# verify real KMS modeset and PageFlip completion on this native ISA.
python3 tests/native-drm.py "$arch"
e2fsck -fn "$disk"
echo "PASS: $arch upstream libdrm/GEM and KMS PageFlip verified after GUI roundtrip"
# Isolate original X11 packages in a DIFFERENT authentic Alpine image.
# CPU-emulated ARM64/RV64 startup of the large Xorg font/module set must not
# affect the already-proven Weston's startup and physical Thunar GUI tests.
sh tests/native-xorg.sh "$arch"
