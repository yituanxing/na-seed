#!/bin/sh
# Source-only, three-ISA QEMU user-network option owner. The caller owns the
# architecture's actual VirtIO device model and argv; this module validates
# and composes the identical user/slirp netdev and PCAP strings for every ISA.
# No eval, shell word splitting of QEMU fragments, or hidden NIC is allowed.

nas_qemu_netdev_port() {
    nas_qemu_netdev_number=$1
    case "$nas_qemu_netdev_number" in
        ''|*[!0-9]*) return 1 ;;
    esac
    while [ "${nas_qemu_netdev_number#0}" != "$nas_qemu_netdev_number" ]; do
        nas_qemu_netdev_number=${nas_qemu_netdev_number#0}
    done
    [ -n "$nas_qemu_netdev_number" ] &&
        [ "${#nas_qemu_netdev_number}" -le 5 ] &&
        [ "$nas_qemu_netdev_number" -le 65535 ] || return 1
    printf '%s' "$nas_qemu_netdev_number"
}

nas_qemu_netdev_legacy_pair() {
    nas_qemu_netdev_proto=$1
    nas_qemu_netdev_host_var=$2
    nas_qemu_netdev_guest_var=$3
    nas_qemu_netdev_host_raw=$4
    nas_qemu_netdev_guest_raw=$5
    nas_qemu_netdev_default_guest=$6
    if [ -z "$nas_qemu_netdev_host_raw" ] &&
       [ -n "$nas_qemu_netdev_guest_raw" ]; then
        echo "$nas_qemu_netdev_guest_var requires $nas_qemu_netdev_host_var" >&2
        return 2
    fi
    [ -n "$nas_qemu_netdev_host_raw" ] || return 0
    [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
        echo "$nas_qemu_netdev_host_var requires NAS_VIRTIO_NET_USER=1" >&2
        return 2
    }
    case "$nas_qemu_netdev_host_raw" in
        ''|*[!0-9]*) echo "$nas_qemu_netdev_host_var must be decimal 1..65535" >&2; return 2 ;;
    esac
    nas_qemu_netdev_host=$(nas_qemu_netdev_port "$nas_qemu_netdev_host_raw") || {
        echo "$nas_qemu_netdev_host_var out of range 1..65535" >&2
        return 2
    }
    nas_qemu_netdev_guest_raw=${nas_qemu_netdev_guest_raw:-$nas_qemu_netdev_default_guest}
    case "$nas_qemu_netdev_guest_raw" in
        ''|*[!0-9]*) echo "$nas_qemu_netdev_guest_var must be decimal 1..65535" >&2; return 2 ;;
    esac
    nas_qemu_netdev_guest=$(nas_qemu_netdev_port "$nas_qemu_netdev_guest_raw") || {
        echo "$nas_qemu_netdev_guest_var out of range 1..65535" >&2
        return 2
    }
    nas_qemu_netdev_forward_suffix="${nas_qemu_netdev_forward_suffix},hostfwd=${nas_qemu_netdev_proto}:127.0.0.1:${nas_qemu_netdev_host}-:${nas_qemu_netdev_guest}"
}

nas_qemu_netdev_prepare() {
    nas_qemu_netdev_primary=
    nas_qemu_netdev_secondary=
    nas_qemu_netdev_capture=
    nas_qemu_netdev_forward_suffix=

    nas_qemu_netdev_legacy_pair tcp \
        NAS_VIRTIO_NET_HOSTFWD_PORT NAS_VIRTIO_NET_GUESTFWD_PORT \
        "${NAS_VIRTIO_NET_HOSTFWD_PORT:-}" "${NAS_VIRTIO_NET_GUESTFWD_PORT:-}" \
        15432 || return 2
    nas_qemu_netdev_legacy_pair udp \
        NAS_VIRTIO_NET_HOSTFWD_UDP_PORT NAS_VIRTIO_NET_GUESTFWD_UDP_PORT \
        "${NAS_VIRTIO_NET_HOSTFWD_UDP_PORT:-}" "${NAS_VIRTIO_NET_GUESTFWD_UDP_PORT:-}" \
        15453 || return 2
    if [ -n "${NAS_VIRTIO_NET_EXTRA_HOSTFWD:-}" ]; then
        nas_qemu_netdev_forward_suffix=$(sh scripts/qemu-hostfwd-extra.sh \
            "$nas_qemu_netdev_forward_suffix") || return 2
    fi

    if [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ]; then
        nas_qemu_netdev_primary="user,id=naseed_net${nas_qemu_netdev_forward_suffix}"
    fi
    if [ "${NAS_VIRTIO_NET_SECOND_USER:-0}" = 1 ]; then
        [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
            echo 'NAS_VIRTIO_NET_SECOND_USER requires NAS_VIRTIO_NET_USER=1' >&2
            return 2
        }
        nas_qemu_netdev_secondary='user,id=naseed_net2,net=10.0.3.0/24'
    fi
    if [ -n "${NAS_VIRTIO_NET_PCAP:-}" ]; then
        [ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] || {
            echo 'NAS_VIRTIO_NET_PCAP requires NAS_VIRTIO_NET_USER=1' >&2
            return 2
        }
        nas_qemu_netdev_capture="filter-dump,id=naseed_wire_capture,netdev=naseed_net,file=$NAS_VIRTIO_NET_PCAP"
    fi
}
