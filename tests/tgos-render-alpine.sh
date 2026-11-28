#!/bin/sh
# Real x86_64 NaSeed kernel + unmodified Alpine libdrm userspace + real
# QEMU virtio-gpu-gl-pci. No fake render ioctl, no synthetic host data.
set -eu
[ "$#" -eq 0 ] || { echo "usage: sh tests/tgos-render-alpine.sh" >&2; exit 2; }

work="$PWD/build/tgos-render-alpine"
out="$work/kernel"
root="$work/rootfs"
version=3.23.7
archive="alpine-minirootfs-$version-x86_64.tar.gz"
url="https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/x86_64/$archive"
mkdir -p "$work"
make ARCH=x86_64 OUT="$out" NAS_ENABLE_VIRTIO_GPU=1 \
    NAS_EXPERIMENT_TGOS_DRM_FFI=1 NAS_EXPERIMENT_TGOS_GPU_OWNER=1 iso

curl -fLsS --retry 3 -o "$work/$archive" "$url"
curl -fLsS --retry 3 -o "$work/$archive.sha256" "$url.sha256"
(cd "$work" && sha256sum -c "$archive.sha256")
sudo rm -rf -- "$root"
sudo mkdir -p "$root"
sudo tar --numeric-owner -xzf "$work/$archive" -C "$root"
# Genuine signed Alpine Python and libdrm. The test file is only a workload;
# these shared libraries and the libc are not modified.
docker run --rm -v "$root:/target" alpine:3.23 sh -ec '
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch x86_64 --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \
      -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
      -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
      add python3 libdrm
'
sudo mkdir -p "$root/root" "$root/dev" "$root/proc" "$root/tmp"
sudo cp tests/tgos-render-guest.py "$root/root/tgos-render-guest.py"
test -x "$root/usr/bin/python3"
test -e "$root/usr/lib/libdrm.so.2"
test -e "$root/lib/ld-musl-x86_64.so.1"
(cd "$root" && sudo find . -print | LC_ALL=C sort |
    sudo cpio -o -H newc --quiet --owner=0:0) > "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"

cp "$work/rootfs.cpio" "$out/iso/boot/rootfs.cpio"
cat > "$out/iso/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/usr/bin/python3 /root/tgos-render-guest.py
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image \
    -o "$work/tgos-render-alpine.iso" "$out/iso"

ovmf=""
for path in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd; do
    if [ -f "$path" ]; then ovmf="$path"; break; fi
done
test -n "$ovmf"
rc=0
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
timeout 190s xvfb-run -a -s "-screen 0 1280x800x24 +extension GLX" \
    qemu-system-x86_64 -M q35 -m 1024M -cpu qemu64 \
    -display sdl,gl=on -monitor none -serial stdio -no-reboot -no-shutdown -nic none \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" \
    -cdrom "$work/tgos-render-alpine.iso" -vga none \
    -device virtio-gpu-gl-pci,disable-legacy=on \
    > "$work/serial.log" 2>&1 || rc=$?

cat "$work/serial.log"
# Debug exit 0 maps to QEMU isa-debug-exit process status 1.
[ "$rc" -eq 1 ] || {
    echo "FAIL: actual Alpine userspace did not exit 0, qemu=$rc" >&2
    exit 1
}
for marker in \
    "TGOS_ALPINE_LIBDRM_RENDER_OPEN_PASS" \
    "TGOS_ALPINE_GETPARAM_INT_ABI_PASS" \
    "TGOS_ALPINE_REAL_GETCAPS_PASS" \
    "TGOS_ALPINE_GETCAPS_SHORT_BUFFER_PASS" \
    "TGOS_ALPINE_EXECBUFFER_NOT_READY_CONFIRMED" \
    "TGOS_ALPINE_RENDER_USERSPACE_PASS" \
    "[TGOS ORIGINAL GPU 3D RESOURCE FENCE PASS]" \
    "[PASS] cmdline userspace exit 0"; do
    grep -Fq "$marker" "$work/serial.log" || {
        echo "FAIL: genuine Alpine GPU test missing marker $marker" >&2
        exit 1
    }
done
if grep -Fq "[FAIL]" "$work/serial.log"; then
    echo "FAIL: NaSeed kernel printed failure" >&2
    exit 1
fi
echo "PASS: unmodified Alpine libdrm invoked NaSeed renderD128 and got actual QEMU capset"
echo "NOT a Mesa render, GEM/EXECBUFFER, hardware scanout, or glmark2 PASS."
