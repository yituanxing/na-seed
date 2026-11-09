#!/bin/sh
# M253 original signed Alpine 3ISA multiuser DAC/SSH acceptance.
# Only routes the M241 trusted media setup through new M253 oracles.
set -eu
if [ "$#" -ne 1 ]; then
  echo 'usage: sh scripts/probe-m253-alpine-multiuser.sh amd64|riscv64|arm64' >&2
  exit 2
fi
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
export NAS_M253_MULTIUSER=1 NAS_M244_SSH_REBOOT=1 NAS_M243_CLI_PERSISTENCE=1
export NAS_M242_REBOOT_ROUNDTRIP=1 NAS_M242_PLATFORM_REBOOT=1
export NAS_M242_GUEST_REBOOT=1
export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
export NAS_M241_BUILD_TAG=m253-alpine-multiuser
export NAS_M241_QEMU_TIMEOUT="${NAS_M253_QEMU_TIMEOUT:-1740}"
export NAS_M253_DIAG_SECONDS="${NAS_M253_DIAG_SECONDS:-1530}"
export NAS_M244_HOST_READY_TIMEOUT="${NAS_M244_HOST_READY_TIMEOUT:-800}"
export NAS_M244_HOST_SSH_TIMEOUT="${NAS_M244_HOST_SSH_TIMEOUT:-240}"
exec sh scripts/probe-m241-alpine-poweroff.sh "$1"
