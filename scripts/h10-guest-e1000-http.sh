#!/bin/sh
# Executed by the signed original dynamic Alpine BusyBox from the H10 NVMe
# ext4 root. The random HTTP source exists ONLY on the CI host; its SHA-256
# cannot be precomputed or hidden in the guest image.
set -eu

test "$(/bin/busybox cat /root/h10-disk-only.txt)" = \
    'H10_E1000_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox sh /root/h8-workload.sh

# lwIP's DHCP network worker starts asynchronously. Retry the original
# Alpine BusyBox wget (not a kernel-side synthetic packet or bundled source)
# for a bounded number of seconds. A failed fetch hashes an empty response,
# which the independent CI host's fresh unpredictable digest will reject.
attempt=0
hash=''
while [ "$attempt" -lt 25 ]; do
    hash=$(
        /bin/busybox wget -q -T 3 -O - \
            http://10.0.2.2:15431/wget 2>/dev/null |
        /bin/busybox sha256sum
    )
    set -- $hash
    hash=$1
    if [ "$hash" != \
         'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' ]; then
        break
    fi
    attempt=$((attempt + 1))
    /bin/busybox sleep 1
done
if [ "$attempt" -ge 25 ]; then
    /bin/busybox echo H10_E1000_HTTP_TIMEOUT
    exit 1
fi
/bin/busybox echo "H10_E1000_HTTP_SHA256=$hash"
/bin/busybox echo H10_E1000_ORIGINAL_ALPINE_WGET_PASS
