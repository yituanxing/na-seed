#!/bin/sh
set -eu
# Keep each provider linked to its existing real implementation and fixture.
sh scripts/check-host-fixture.sh netlink-filter
sh scripts/check-host-fixture.sh netlink-generic
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for protocol in route diag; do
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -DNAS_ENABLE_LWIP=1 \
        -fno-builtin -I kernel "scripts/test-netlink-$protocol.c" \
        kernel/fs/netlink_route.c kernel/lib/string.c -o "$tmp/$protocol"
    "$tmp/$protocol"
done
echo '[NETLINK PROVIDERS HOST PASS] ROUTE, SOCK_DIAG, NETFILTER, GENERIC'
