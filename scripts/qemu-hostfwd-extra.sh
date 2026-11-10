#!/bin/sh
# Append several validated localhost-only slirp forwards to ONE existing NIC.
# NAS_VIRTIO_NET_EXTRA_HOSTFWD is a comma-separated list of:
#   tcp:HOST_PORT:GUEST_PORT,udp:HOST_PORT:GUEST_PORT
# Legacy single TCP/UDP variables remain the authoritative default path.
set -eu

fail() {
    echo "NAS_VIRTIO_NET_EXTRA_HOSTFWD: $*" >&2
    exit 2
}

port() {
    nas_extra_port=$1
    case "$nas_extra_port" in
        ''|*[!0-9]*) return 1 ;;
    esac
    while [ "${nas_extra_port#0}" != "$nas_extra_port" ]; do
        nas_extra_port=${nas_extra_port#0}
    done
    [ -n "$nas_extra_port" ] && [ "${#nas_extra_port}" -le 5 ] &&
        [ "$nas_extra_port" -le 65535 ] || return 1
    printf '%s' "$nas_extra_port"
}

[ "$#" -eq 1 ] || fail 'expected existing NIC forwarding suffix'
nas_extra_suffix=$1
nas_extra_rules=${NAS_VIRTIO_NET_EXTRA_HOSTFWD:-}
if [ -z "$nas_extra_rules" ]; then
    printf '%s\n' "$nas_extra_suffix"
    exit 0
fi
[ "${NAS_VIRTIO_NET_USER:-0}" = 1 ] ||
    fail 'requires NAS_VIRTIO_NET_USER=1'
case "$nas_extra_rules" in
    ,*|*,|*,,*|*[!abcdefghijklmnopqrstuvwxyz0123456789:,]*)
        fail 'expected comma-separated tcp|udp:HOST_PORT:GUEST_PORT rules' ;;
esac

# Split only validated punctuation; never evaluate strings as shell commands,
# host addresses, globs or arbitrary QEMU options.
set -f
nas_extra_ifs=$IFS
IFS=,
set -- $nas_extra_rules
IFS=$nas_extra_ifs
[ "$#" -le 12 ] || fail 'at most 12 extra rules are allowed'

# A duplicate host TCP or UDP bind must fail before QEMU starts. A host port
# can legitimately serve both TCP and UDP, so the protocol is part of the key.
nas_extra_seen='|'
for nas_extra_legacy in "tcp:${NAS_VIRTIO_NET_HOSTFWD_PORT:-}" \
                        "udp:${NAS_VIRTIO_NET_HOSTFWD_UDP_PORT:-}"; do
    nas_extra_proto=${nas_extra_legacy%%:*}
    nas_extra_raw=${nas_extra_legacy#*:}
    if [ -n "$nas_extra_raw" ]; then
        nas_extra_host=$(port "$nas_extra_raw") || fail 'invalid legacy host port'
        nas_extra_seen="${nas_extra_seen}${nas_extra_proto}:${nas_extra_host}|"
    fi
done

for nas_extra_rule in "$@"; do
    nas_extra_proto=${nas_extra_rule%%:*}
    nas_extra_pair=${nas_extra_rule#*:}
    [ "$nas_extra_pair" != "$nas_extra_rule" ] || fail 'missing protocol delimiter'
    case "$nas_extra_proto" in tcp|udp) ;; *) fail 'protocol must be tcp or udp' ;; esac
    nas_extra_rawhost=${nas_extra_pair%%:*}
    nas_extra_rawguest=${nas_extra_pair#*:}
    [ "$nas_extra_rawguest" != "$nas_extra_pair" ] || fail 'missing guest delimiter'
    case "$nas_extra_rawguest" in *:*) fail 'too many delimiters' ;; esac
    nas_extra_host=$(port "$nas_extra_rawhost") || fail 'invalid host port'
    nas_extra_guest=$(port "$nas_extra_rawguest") || fail 'invalid guest port'
    case "$nas_extra_seen" in
        *"|${nas_extra_proto}:${nas_extra_host}|"*)
            fail 'duplicate host protocol/port binding' ;;
    esac
    nas_extra_seen="${nas_extra_seen}${nas_extra_proto}:${nas_extra_host}|"
    nas_extra_suffix="${nas_extra_suffix},hostfwd=${nas_extra_proto}:127.0.0.1:${nas_extra_host}-:${nas_extra_guest}"
done
printf '%s\n' "$nas_extra_suffix"
