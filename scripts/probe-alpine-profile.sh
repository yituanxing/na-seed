#!/bin/sh
# One launcher for the original Alpine cohort and reboot profiles.
set -eu
profile=${1-}
[ "$#" -gt 0 ] || exit 2
shift
case "$profile" in
    m204|m205|m206) selector=COMPAT_COHORT ;;
    m215|m218|m219|m220|m221|m222|m223|m224) selector=PILOT ;;
    m225) selector=PILOT ;;
    m226) selector=PILOT ;;
    m235|m236|m237|m238) selector=COMPAT_COHORT ;;
    m242) selector= ;;
    m243) selector= ;;
    m244) selector= ;;
    m253) selector= ;;
    *) exit 2 ;;
esac
if [ "$#" -ne 1 ]; then
    case "$profile" in m235|m236|m237|m238) ;; *)
        echo "usage: sh scripts/probe-alpine-profile.sh $profile amd64|riscv64|arm64" >&2 ;;
    esac
    exit 2
fi
case "$profile" in m244|m253) case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac ;; esac
if [ -n "$selector" ]; then
    export "NAS_M${profile#m}_${selector}=1"
    export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
    exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
fi
export NAS_M242_REBOOT_ROUNDTRIP=1 NAS_M242_PLATFORM_REBOOT=1 NAS_M242_GUEST_REBOOT=1
case "$profile" in
    m243|m244|m253)
        export NAS_M243_CLI_PERSISTENCE=1
        export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1 ;;
esac
case "$profile" in m244|m253) export NAS_M244_SSH_REBOOT=1 ;; esac
case "$profile" in
    m242) export NAS_M241_BUILD_TAG=m242-reboot NAS_M241_QEMU_TIMEOUT="${NAS_M242_QEMU_TIMEOUT:-430}" ;;
    m243) export NAS_M241_BUILD_TAG=m243-alpine-cli NAS_M241_QEMU_TIMEOUT="${NAS_M243_QEMU_TIMEOUT:-830}" ;;
    m244)
        export NAS_M241_BUILD_TAG=m244-alpine-ssh NAS_M241_QEMU_TIMEOUT="${NAS_M244_QEMU_TIMEOUT:-1110}"
        export NAS_M244_DIAG_SECONDS="${NAS_M244_DIAG_SECONDS:-965}" ;;
    m253)
        export NAS_M253_MULTIUSER=1 NAS_M241_BUILD_TAG=m253-alpine-multiuser
        export NAS_M241_QEMU_TIMEOUT="${NAS_M253_QEMU_TIMEOUT:-1740}"
        export NAS_M253_DIAG_SECONDS="${NAS_M253_DIAG_SECONDS:-1530}"
        export NAS_M244_HOST_READY_TIMEOUT="${NAS_M244_HOST_READY_TIMEOUT:-800}"
        export NAS_M244_HOST_SSH_TIMEOUT="${NAS_M244_HOST_SSH_TIMEOUT:-240}" ;;
esac
exec sh scripts/probe-m241-alpine-poweroff.sh "$1"
