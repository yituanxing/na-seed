#!/bin/sh
set -eu
iso="$1"
ovmf=""
for candidate in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd; do
    if [ -f "$candidate" ]; then ovmf="$candidate"; break; fi
done
if [ -z "$ovmf" ]; then
    echo "OVMF not found. Install the ovmf package." >&2
    exit 2
fi
# An explicit raw image is a real block device, NOT a Limine RAM module.
# Default tests retain the original QEMU options and hardware set.
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    test -s "$NAS_VIRTIO_BLK_IMAGE"
    set -- -drive "if=none,id=naseed_ext4,format=raw,file=$NAS_VIRTIO_BLK_IMAGE" \
           -device virtio-blk-pci,drive=naseed_ext4,disable-legacy=on
else
    set --
fi
if [ -n "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ]; then
    test -s "$NAS_QEMU_ASYNC_BLK_IMAGE"
    set -- "$@" -drive "if=none,id=m178_async,format=raw,file=$NAS_QEMU_ASYNC_BLK_IMAGE" \
        -device virtio-blk-pci,drive=m178_async,disable-legacy=on
fi
# M228: one shared, fail-closed three-ISA M228/M233 slirp/hostfwd owner.
. scripts/acceptance/qemu-netdev.sh
nas_qemu_netdev_prepare || exit 2
if [ -n "$nas_qemu_netdev_primary" ]; then
    set -- "$@" -netdev "$nas_qemu_netdev_primary" \
        -device virtio-net-pci,netdev=naseed_net,disable-legacy=on
fi
if [ "${NAS_VIRTIO_NET_SECOND_USER:-0}" = 1 ]; then
    set -- "$@" -netdev "$nas_qemu_netdev_secondary" \
        -device virtio-net-pci,netdev=naseed_net2,disable-legacy=on
fi
if [ -n "$nas_qemu_netdev_capture" ]; then
    set -- "$@" -object "$nas_qemu_netdev_capture"
fi
# H2 resource inventory: a controller without any namespace/drive. The guest
# may read PCI configuration metadata but cannot touch a host or guest disk.
# Normal QEMU profiles are unchanged unless explicitly opted in.
if [ "${NAS_QEMU_H2_NVME:-0}" = 1 ]; then
    [ "${NAS_QEMU_H4_NVME:-0}" != 1 ] || {
        echo 'H2 controller-only and H4 namespace profiles are exclusive' >&2
        exit 2
    }
    [ "${NAS_QEMU_H5_PCI_NVME:-0}" != 1 ] || {
        echo 'H2 controller-only and H5 PCI namespace profiles are exclusive' >&2
        exit 2
    }
    [ "${NAS_QEMU_H6_TWO_NVME:-0}" != 1 ] || {
        echo 'H2 and H6 ambiguous PCI profiles are exclusive' >&2
        exit 2
    }
    set -- "$@" -device nvme,id=naseed_h2_nvme,serial=naseedh2
fi
# H4: a known, generated one-megabyte namespace fixture, explicitly mounted
# read-only. NEVER accept a caller-supplied raw disk/device path. This profile
# is a QEMU-only experiment, not a physical-machine hardware path.
if [ "${NAS_QEMU_H4_NVME:-0}" = 1 ]; then
    [ "${NAS_QEMU_H5_PCI_NVME:-0}" != 1 ] || {
        echo 'H4 fixed-BDF and H5 relocated PCI profiles are exclusive' >&2
        exit 2
    }
    [ "${NAS_QEMU_H6_TWO_NVME:-0}" != 1 ] || {
        echo 'H4 and H6 ambiguous PCI profiles are exclusive' >&2
        exit 2
    }
    h4_image=build/h4-qemu-only/h4-seed.img
    sh scripts/check-h4-nvme-seed.sh || exit 2
    set -- "$@" \
        -drive "if=none,id=naseed_h4_disk,format=raw,file=$h4_image,readonly=on" \
        -device nvme,id=naseed_h2_nvme,serial=naseedh2 \
        -device nvme-ns,drive=naseed_h4_disk,nsid=1
fi
# H5: same fixed, disposable, read-only namespace, but the NVMe endpoint is
# deliberately moved away from H4's 00:03.0 PCI function. A harmless PCI
# balloon-function decoy occupies slot 3. Dynamic PCI BAR qualification is
# exercised by the guest; no caller-selected block images or physical DMA.
if [ "${NAS_QEMU_H5_PCI_NVME:-0}" = 1 ]; then
    [ "${NAS_QEMU_H6_TWO_NVME:-0}" != 1 ] || {
        echo 'H5 positive and H6 ambiguous PCI profiles are exclusive' >&2
        exit 2
    }
    h5_image=build/h4-qemu-only/h4-seed.img
    sh scripts/check-h4-nvme-seed.sh || exit 2
    set -- "$@" \
        -device virtio-balloon-pci,id=naseed_h5_decoy,disable-legacy=on,bus=pcie.0,addr=0x3 \
        -drive "if=none,id=naseed_h5_disk,format=raw,file=$h5_image,readonly=on" \
        -device nvme,id=naseed_h5_nvme,serial=naseedh5,bus=pcie.0,addr=0x5 \
        -device nvme-ns,drive=naseed_h5_disk,nsid=1
fi
# H7: one brand-new, scripted and bounded ext4 image, with only verified
# original Debian BusyBox/Bash ELF files.  This is intentionally NOT the H4
# fixed LBA seed, an arbitrary disk, or a physical NVMe device.  QEMU alone
# presents the generated image read-only at the same moved PCI BDF as H5/H6.
if [ "${NAS_QEMU_H7_NVME:-0}" = 1 ]; then
    [ "${NAS_QEMU_H2_NVME:-0}" = 0 ] &&
    [ "${NAS_QEMU_H4_NVME:-0}" = 0 ] &&
    [ "${NAS_QEMU_H5_PCI_NVME:-0}" = 0 ] &&
    [ "${NAS_QEMU_H6_TWO_NVME:-0}" = 0 ] &&
    [ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
    [ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] || {
        echo 'H7 fixed read-only NVMe and other controller/block profiles are exclusive' >&2
        exit 2
    }
    h7_image=build/h7-qemu-only/h7-ext4.img
    test -s "$h7_image" && test -s build/h7-qemu-only/h7-media-before.sha256 || {
        echo 'H7 fixed, generated read-only ext4 fixture missing' >&2
        exit 2
    }
    sha256sum -c build/h7-qemu-only/h7-media-before.sha256 >/dev/null || {
        echo 'H7 fixed NVMe image checksum mismatch before QEMU' >&2
        exit 2
    }
    set -- "$@" \
        -device virtio-balloon-pci,id=naseed_h7_decoy,disable-legacy=on,bus=pcie.0,addr=0x3 \
        -drive "if=none,id=naseed_h7_disk,format=raw,file=$h7_image,readonly=on" \
        -device nvme,id=naseed_h7_nvme,serial=naseedh7,bus=pcie.0,addr=0x5 \
        -device nvme-ns,drive=naseed_h7_disk,nsid=1
fi
# H6 fail-closed acceptance: two controllers with zero namespaces and zero
# backing drives. The guest may enumerate both via read-only PCI config, but
# must refuse write-capable NVMe initialization before touching either BAR or
# starting DMA. No arbitrary host storage paths are accepted by this profile.
if [ "${NAS_QEMU_H6_TWO_NVME:-0}" = 1 ]; then
    set -- "$@" \
        -device nvme,id=naseed_h6_nvme_a,serial=naseedh6a,bus=pcie.0,addr=0x5 \
        -device nvme,id=naseed_h6_nvme_b,serial=naseedh6b,bus=pcie.0,addr=0x6
fi
# Real QEMU input devices, separate from the serial console and RAMFB.
# The default headless device profile remains unchanged.
if [ "${NAS_QEMU_INPUT_OPTIN:-0}" = 1 ]; then
    set -- "$@" -device virtio-keyboard-pci,disable-legacy=on \
        -device virtio-tablet-pci,disable-legacy=on \
        -device virtio-mouse-pci,disable-legacy=on
fi
# Explicit graphics acceptance only. The 2D VirtIO-GPU profile and historical
# firmware ramfb are mutually exclusive real QEMU devices, not aliases.
if [ "${NAS_QEMU_VIRTIO_GPU:-0}" = 1 ]; then
    [ "${NAS_QEMU_RAMFB:-0}" != 1 ] || {
        echo 'GPU and ramfb acceptance devices cannot be combined' >&2
        exit 2
    }
    set -- "$@" -vga none -device virtio-gpu-pci,disable-legacy=on,xres=800,yres=600
elif [ "${NAS_QEMU_RAMFB:-0}" = 1 ]; then
    set -- "$@" -vga none -device ramfb
elif [ -n "${NAS_QEMU_QMP_SOCKET:-}" ]; then
    echo 'NAS_QEMU_QMP_SOCKET requires a real graphics device' >&2
    exit 2
fi
if [ -n "${NAS_QEMU_QMP_SOCKET:-}" ]; then
    set -- "$@" -qmp "unix:$NAS_QEMU_QMP_SOCKET,server=on,wait=off"
fi
if [ "${NAS_M242_GUEST_REBOOT:-0}" != 1 ]; then
    set -- "$@" -no-reboot -no-shutdown
fi
exec qemu-system-x86_64 -M q35 -m "${NAS_ACCEPTANCE_QEMU_RAM:-256M}" -cpu qemu64 \
    -display none -monitor none -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" -cdrom "$iso" "$@"
