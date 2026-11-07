#!/bin/sh
# M228: the ORIGINAL signed Alpine lighttpd/OpenRC service from M227 serves a
# real incoming TCP connection through QEMU's existing VirtIO-Net/slirp NIC.
# The guest only provides a bounded live observation window; the independent
# host must initiate and verify an HTTP GET while this QEMU is still running.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
fail() { echo "M228_ERROR $*" >&2; exit 1; }

case "${1:-}" in
    --first)
        NAS_M228_EXPOSE_ETH0=1 /bin/sh /root/m227-alpine-openrc-workload.sh --first ||
            fail 'original first-boot signed OpenRC HTTP cohort'
        phase=FIRST
        ;;
    --reopen)
        # The original second Alpine PID1/boot+default runlevels must already
        # own the service. M227 --reopen does not call rc-service start.
        NAS_M228_EXPOSE_ETH0=1 /bin/sh /root/m227-alpine-openrc-workload.sh --reopen ||
            fail 'automatic second-boot signed OpenRC HTTP cohort'
        phase=REOPEN
        ;;
    *) fail 'usage: --first|--reopen' ;;
esac

$busy grep -qx 'server.bind = "0.0.0.0"' /etc/lighttpd/lighttpd.conf ||
    fail 'real eth0 listener not configured'
rc-service lighttpd status || fail 'original lighttpd not running for host GET'
test -s /run/lighttpd.pid &&
    $busy kill -0 "$($busy cat /run/lighttpd.pid)" ||
    fail 'original lighttpd process absent for host GET'

# The host watcher scans the actual serial device log for this line (it is not
# present in the UART command string), then sends its unique nonce-bearing
# HTTP request through hostfwd. Guest cannot manufacture the host receipt.
case "$phase" in
    FIRST) echo M228_FIRST_HOST_INGRESS_READY ;;
    REOPEN) echo M228_REOPEN_HOST_INGRESS_READY ;;
esac
$busy sleep 45
rc-service lighttpd status || fail 'original lighttpd stopped inside host window'
case "$phase" in
    FIRST) echo M228_FIRST_HOST_INGRESS_WINDOW_COMPLETE ;;
    REOPEN) echo M228_REOPEN_HOST_INGRESS_WINDOW_COMPLETE ;;
esac
