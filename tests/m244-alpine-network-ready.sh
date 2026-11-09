#!/bin/sh
# Minimal original Alpine ifupdown-ng pre-up hook already proven by M227.
# NaSeed's real kernel lwIP owns eth0, DHCP and the route; do not pretend
# to configure hardware through unsupported SIOCSIFFLAGS ioctls.
set -eu
n=0
while [ "$n" -lt 90 ]; do
    if grep -q '^nameserver 10[.]0[.]2[.]3$' /proc/net/naseed-resolv.conf 2>/dev/null &&
       grep -q 'eth0:' /proc/net/dev 2>/dev/null; then
        exit 0
    fi
    n=$((n + 1))
    sleep 1
done
echo '[M244 NETWORK FAIL] physical lwIP eth0/DHCP never became ready' >&2
exit 1
