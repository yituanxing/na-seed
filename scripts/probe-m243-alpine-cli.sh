#!/bin/sh
# M243 composes the existing M196 public apk, M194 original OpenRC,
# and M242 SAME-process firmware-reset owners; no desktop or replacement PID1.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m243-alpine-cli.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M243_CLI_PERSISTENCE=1 NAS_M242_REBOOT_ROUNDTRIP=1
export NAS_M242_PLATFORM_REBOOT=1 NAS_M242_GUEST_REBOOT=1
export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
export NAS_M241_BUILD_TAG=m243-alpine-cli
export NAS_M241_QEMU_TIMEOUT="${NAS_M243_QEMU_TIMEOUT:-830}"
exec sh scripts/probe-m241-alpine-poweroff.sh "$1"
