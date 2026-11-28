#!/bin/sh
# Real x86_64 NaSeed kernel + unmodified Alpine libdrm userspace + real
# QEMU virtio-gpu-gl-pci. No fake render ioctl, no synthetic host data.
set -eu
[ "$#" -eq 0 ] || { echo "usage: sh tests/tgos-render-alpine.sh" >&2; exit 2; }

mesa_mode="${TGOS_MESA_PROBE:-0}"
[ "$mesa_mode" = "0" ] || [ "$mesa_mode" = "1" ] || exit 2
if [ "$mesa_mode" = "1" ]; then
    work="$PWD/build/tgos-mesa-alpine"
    guest_script=tgos-mesa-guest.py
    extra_packages="mesa-dri-gallium mesa-egl mesa-gbm mesa-gles"
    memory=2048M
    timeout_seconds=110
else
    work="$PWD/build/tgos-render-alpine"
    guest_script=tgos-render-guest.py
    extra_packages=""
    memory=1024M
    timeout_seconds=190
fi
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
docker run --rm -v "$root:/target" -e "MESA_EXTRA_PACKAGES=$extra_packages" alpine:3.23 sh -ec '
    cp -a /target/etc/apk/keys/. /etc/apk/keys/
    apk --arch x86_64 --root /target --keys-dir /etc/apk/keys --initdb --no-scripts \
      -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
      -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
      add python3 libdrm $MESA_EXTRA_PACKAGES
'
sudo mkdir -p "$root/root" "$root/dev" "$root/proc" "$root/tmp"
sudo cp "tests/$guest_script" "$root/root/$guest_script"
test -x "$root/usr/bin/python3"
test -e "$root/usr/lib/libdrm.so.2"
test -e "$root/lib/ld-musl-x86_64.so.1"
(cd "$root" && sudo find . -print | LC_ALL=C sort |
    sudo cpio -o -H newc --quiet --owner=0:0) > "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"

cp "$work/rootfs.cpio" "$out/iso/boot/rootfs.cpio"
cat > "$out/iso/boot/limine/limine.conf" <<LIMINE
timeout: 0
/NaSeed
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/usr/bin/python3 /root/$guest_script
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
timeout "${timeout_seconds}s" xvfb-run -a -s "-screen 0 1280x800x24 +extension GLX" \
    qemu-system-x86_64 -M q35 -m "$memory" -cpu qemu64 \
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
if [ "$mesa_mode" = "1" ]; then
    for marker in \
        "TGOS_MESA_GBM_RENDER_NODE_PASS" \
        "TGOS_MESA_EGL_INITIALIZE_PASS" \
        "TGOS_MESA_VIRGL_RENDERER_PASS" \
        "TGOS_MESA_GL_CLEAR_PIXELS_PASS" \
        "TGOS_MESA_REAL_APPLICATION_PASS" \
        "[PASS] cmdline userspace exit 0"; do
        grep -Fq "$marker" "$work/serial.log" || {
            echo "FAIL: genuine Mesa EGL app missing marker $marker" >&2
            exit 1
        }
    done
else
for marker in \
    "TGOS_ALPINE_LIBDRM_RENDER_OPEN_PASS" \
    "TGOS_ALPINE_GETPARAM_INT_ABI_PASS" \
    "TGOS_ALPINE_REAL_GETCAPS_PASS" \
    "TGOS_ALPINE_GETCAPS_SHORT_BUFFER_PASS" \
    "TGOS_ALPINE_NONEMPTY_EXECBUFFER_FENCE_PASS" \
    "TGOS_ALPINE_MULTI_BO_FENCED_EXECBUFFER_PASS" \
    "TGOS_ALPINE_REAL_RESOURCE_CREATE_INFO_EXEC_PASS" \
    "TGOS_ALPINE_REAL_BO_MMAP_PAGE_ISOLATION_PASS" \
    "TGOS_ALPINE_GPU_HOST_DMA_ROUNDTRIP_PASS" \
    "TGOS_ALPINE_GPU_TRANSFER_BOUNDS_PASS" \
    "TGOS_ALPINE_GEM_CLOSE_LIVE_VMA_PASS" \
    "TGOS_ALPINE_RESOURCE_FILE_ISOLATION_PASS" \
    "TGOS_ALPINE_GEM_CLOSE_DEVICE_RELEASE_PASS" \
    "TGOS_ALPINE_RENDER_FILE_CONTEXT_LIFETIME_PASS" \
    "TGOS_ALPINE_NONEMPTY_UNSUPPORTED_REJECT_PASS" \
    "TGOS_ALPINE_EXECBUFFER_ZERO_SIZE_REJECT_PASS" \
    "TGOS_ALPINE_RENDER_USERSPACE_PASS" \
    "[TGOS ORIGINAL GPU 3D RESOURCE FENCE PASS]" \
    "[PASS] cmdline userspace exit 0"; do
    grep -Fq "$marker" "$work/serial.log" || {
        echo "FAIL: genuine Alpine GPU test missing marker $marker" >&2
        exit 1
    }
done
fi
if grep -Fq "[FAIL]" "$work/serial.log"; then
    echo "FAIL: NaSeed kernel printed failure" >&2
    exit 1
fi
if [ "$mesa_mode" = "1" ]; then
    echo "PASS: original Alpine Mesa/EGL/GLES2 rendered verified pixels through QEMU VirGL"
else
    echo "PASS: unmodified Alpine libdrm invoked NaSeed renderD128 and got actual QEMU capset"
    echo "Genuine Alpine mmap -> QEMU 3D texture upload -> host DMA readback verified."
fi
