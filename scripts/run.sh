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
# M228: optional REAL host->guest TCP through the existing slirp NIC.
# A loopback-only host listener avoids publishing the test server externally.
# Reject malformed values before they can become QEMU option fragments.
hostfwd=
if [ -n "${NAS_VIRTIO_NET_HOSTFWD_PORT:-}" ]; then
    [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
        echo 'NAS_VIRTIO_NET_HOSTFWD_PORT requires NAS_VIRTIO_NET_USER=1' >&2
        exit 2
    }
    hostfwd_port=$NAS_VIRTIO_NET_HOSTFWD_PORT
    case "$hostfwd_port" in
        *[!0-9]*|'') echo 'NAS_VIRTIO_NET_HOSTFWD_PORT must be decimal 1..65535' >&2; exit 2 ;;
    esac
    while [ "${hostfwd_port#0}" != "$hostfwd_port" ]; do
        hostfwd_port=${hostfwd_port#0}
    done
    [ -n "$hostfwd_port" ] && [ "${#hostfwd_port}" -le 5 ] &&
        [ "$hostfwd_port" -le 65535 ] || {
        echo 'NAS_VIRTIO_NET_HOSTFWD_PORT out of range 1..65535' >&2
        exit 2
    }
    hostfwd=",hostfwd=tcp:127.0.0.1:${hostfwd_port}-:15432"
fi
if [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ]; then
    # Opt-in real modern PCI NIC with QEMU user/slirp DHCP and host gateway.
    # Default QEMU hardware stays unchanged for all historical OS gates.
    set -- "$@" -netdev "user,id=naseed_net${hostfwd}" \
        -device virtio-net-pci,netdev=naseed_net,disable-legacy=on
fi
if [ "${NAS_VIRTIO_NET_SECOND_USER:-0}" = 1 ]; then
    [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
        echo 'NAS_VIRTIO_NET_SECOND_USER requires NAS_VIRTIO_NET_USER=1' >&2
        exit 2
    }
    # Independent slirp subnet proves the two original U-Boot/lwIP netifs
    # receive separate DHCP leases rather than aliasing one NIC's state.
    set -- "$@" -netdev user,id=naseed_net2,net=10.0.3.0/24 \
        -device virtio-net-pci,netdev=naseed_net2,disable-legacy=on
fi
if [ -n "${NAS_VIRTIO_NET_PCAP:-}" ]; then
    [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
        echo 'NAS_VIRTIO_NET_PCAP requires NAS_VIRTIO_NET_USER=1' >&2
        exit 2
    }
    # QEMU's independent libpcap capture distinguishes guest TX, DHCP
    # replies and actual NIC RX without inserting IP logic into the driver.
    set -- "$@" -object \
        "filter-dump,id=naseed_wire_capture,netdev=naseed_net,file=$NAS_VIRTIO_NET_PCAP"
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
exec qemu-system-x86_64 -M q35 -m "${NAS_ACCEPTANCE_QEMU_RAM:-256M}" -cpu qemu64 \
    -display none -monitor none -serial stdio -no-reboot -no-shutdown \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" -cdrom "$iso" "$@"
