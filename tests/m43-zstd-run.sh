#!/bin/sh
# Original Alpine zstd multithreaded binary on NaSeed, not guest-built.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
zstd=/usr/bin/zstd
[ -x "$zstd" ] || zstd=/bin/zstd
"$zstd" --version | /bin/busybox grep -Fq 'Zstandard'
echo M43_NATIVE_ZSTD_PASS

for round in 1 2; do
    # Force worker threads, keeping multichunk input larger than one job.
    "$zstd" -T2 -1 -q -c /tests/m43-input.bin > "/tmp/m43-$round.zst"
    test -s "/tmp/m43-$round.zst"
    "$zstd" -q -d -c "/tmp/m43-$round.zst" > "/tmp/m43-$round.out"
    /bin/busybox cmp /tests/m43-input.bin "/tmp/m43-$round.out"
    /bin/busybox rm "/tmp/m43-$round.out"
    /bin/busybox rm "/tmp/m43-$round.zst"
    echo M43_ZSTD_ROUND_PASS
done
echo M43_THREADS_PASS
