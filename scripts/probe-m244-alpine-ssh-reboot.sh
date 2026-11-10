#!/bin/sh
# M244: original signed, HEADLESS Alpine 3.23 on three native NaSeed QEMUs.
# Real public APK and host interactive OpenSSH PTY must work on BOTH sides
# of one original Alpine /sbin/reboot in the SAME living QEMU process.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m244-alpine-ssh-reboot.sh amd64|riscv64|arm64' >&2
    exit 2
}
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

export NAS_M244_SSH_REBOOT=1 NAS_M243_CLI_PERSISTENCE=1
export NAS_M242_REBOOT_ROUNDTRIP=1 NAS_M242_PLATFORM_REBOOT=1
export NAS_M242_GUEST_REBOOT=1
export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
export NAS_M241_BUILD_TAG=m244-alpine-ssh
export NAS_M241_QEMU_TIMEOUT="${NAS_M244_QEMU_TIMEOUT:-1110}"
export NAS_M244_DIAG_SECONDS="${NAS_M244_DIAG_SECONDS:-965}"

exec sh scripts/probe-m241-alpine-poweroff.sh "$1"
