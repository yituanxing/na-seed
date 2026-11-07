#!/bin/sh
# Two original Alpine PID1 boots with original signed dnsmasq and OpenRC.
# The DNS questions are entirely host-generated; local guest checks are only
# readiness and life-cycle gates, never substitutes for host/PCAP proof.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
fail() { echo "M233_GUEST_FAIL $*" >&2; exit 1; }
case "${1:-}" in --first) phase=FIRST ;; --reopen) phase=REOPEN ;; *) fail phase ;; esac
for package in dnsmasq dnsmasq-openrc; do
    apk info -e "$package" >/dev/null 2>&1 || fail "missing signed $package"
done
program=/usr/sbin/dnsmasq
test -x "$program" || fail 'missing original dnsmasq ELF'
magic="$($busy od -An -tx1 -N4 "$program" | $busy tr -d ' \t\r\n')"
test "$magic" = 7f454c46 || fail 'not original signed ELF'
test -x /etc/init.d/dnsmasq || fail 'official OpenRC script absent'
case "$phase" in
    FIRST)
        /bin/sh /root/m227-alpine-openrc-workload.sh --first || fail 'M227 first boot'
        test ! -e /etc/runlevels/default/dnsmasq || fail 'preenabled DNS OpenRC'
        # This generated config is a disposable, host-loopback-only CI
        # fixture, NOT a safe production resolver. No external recursion.
        $busy cat > /etc/dnsmasq.conf <<'EOF'
port=15453
listen-address=0.0.0.0
bind-interfaces
no-resolv
no-hosts
no-poll
no-dhcp-interface=eth0
user=root
group=root
local=/m233.naseed.test/
address=/m233.naseed.test/198.51.100.233
EOF
        $busy chmod 0644 /etc/dnsmasq.conf
        rc-update add dnsmasq default || fail 'dnsmasq default runlevel registration'
        test -L /etc/runlevels/default/dnsmasq || fail 'DNS runlevel link missing'
        rc-service dnsmasq start || fail 'official Alpine DNS OpenRC start'
        ;;
    REOPEN)
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen || fail 'M227 second boot'
        test -L /etc/runlevels/default/dnsmasq || fail 'DNS OpenRC link not durable'
        ;;
esac
test -f /etc/dnsmasq.conf && test ! -L /etc/dnsmasq.conf || fail 'DNS config absent'
$busy grep -qx 'port=15453' /etc/dnsmasq.conf || fail 'wrong DNS port'
$busy grep -qx 'address=/m233.naseed.test/198.51.100.233' /etc/dnsmasq.conf ||
    fail 'DNS fixture not durable'
rc-service dnsmasq status || fail 'original Alpine dnsmasq not under OpenRC'
case "$phase" in
    FIRST) echo M233_FIRST_HOST_DNS_READY ;;
    REOPEN) echo M233_REOPEN_HOST_DNS_READY ;;
esac
$busy sleep 72
rc-service dnsmasq status || fail 'OpenRC DNS exited during host queries'
case "$phase" in
    FIRST) echo M233_FIRST_HOST_DNS_WINDOW_COMPLETE ;;
    REOPEN) echo M233_REOPEN_HOST_DNS_WINDOW_COMPLETE ;;
esac
