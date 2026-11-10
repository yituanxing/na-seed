#!/bin/sh
# Eight different untouched, signed Alpine ELFs in one real x86 QEMU boot.
# The existing M141 case runner checks each program's actual side effects;
# only then does this cohort publish a distinct per-program PASS marker.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL

failed=0
for entry in \
    tcpdump-pcap:TCPDUMP \
    ssh-agent-socket:SSH_AGENT \
    ssh-add-empty:SSH_ADD \
    ionice-child:IONICE \
    pidwait-child:PIDWAIT \
    exch-files:EXCH \
    bfs-format:MKFS_BFS \
    fdisk-image:FDISK
do
    name=${entry%%:*}
    marker=${entry#*:}
    if /bin/sh /tests/m141-system-run.sh "$name"; then
        /bin/busybox printf 'M141_SIGNED_SYSTEM_%s_PASS\n' "$marker"
    else
        /bin/busybox printf '[M141 SYSTEM COHORT FAIL] %s\n' "$name"
        failed=1
    fi
done

[ "$failed" -eq 0 ] || exit 1
/bin/busybox printf 'M141_SIGNED_SYSTEM_COHORT_PASS\n'
