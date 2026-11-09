#!/bin/sh
# Original signed Alpine, one QEMU PID, real firmware reset and second boot.
# The M241 physical disk construction/verification stays the sole authority.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m242-alpine-reboot.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M242_REBOOT_ROUNDTRIP=1 NAS_M242_PLATFORM_REBOOT=1
export NAS_M242_GUEST_REBOOT=1 NAS_M241_BUILD_TAG=m242-reboot
export NAS_M241_QEMU_TIMEOUT="${NAS_M242_QEMU_TIMEOUT:-430}"
exec sh scripts/probe-m241-alpine-poweroff.sh "$1"
