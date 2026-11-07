#!/bin/sh
# M137's previously proven signed Alpine restic gains NEW REAL REMOTE REST
# and independent second native-QEMU semantics. No new ELF identity claimed.
set -eu
test "$#" -eq 1 || exit 2
case "$1" in --first) phase=first ;; --reopen) phase=reopen ;; *) exit 2 ;; esac
test -s /root/m226-cohort.tsv && test -s /root/m226-restic-workload.sh
test -x /sbin/apk && test -x /bin/busybox
ledger=/root/m226-$phase-results.tsv
test ! -e "$ledger" || exit 1
touch "$ledger"
record() { printf 'M226_RESULT\trestic\tusr/bin/restic\t%s\t%s\n' "$1" "$2" | tee -a "$ledger"; }
test "$(wc -l < /root/m226-cohort.tsv)" -eq 2 || exit 1
grep -Fxq 'restic	usr/bin/restic	remote-backup	restic-rest-repository-two-boot' \
    /root/m226-cohort.tsv || {
    # BusyBox grep interprets no shell escape for the tabs above: use awk.
    awk -F '\t' 'NR==2 && $1=="restic" && $2=="usr/bin/restic" && $3=="remote-backup" && $4=="restic-rest-repository-two-boot" {found=1} END {exit !found}' /root/m226-cohort.tsv
}
if [ "$phase" = first ]; then
    if /sbin/apk info -e restic >/dev/null 2>&1; then
        record install PREEXISTING
        exit 1
    fi
    if /bin/busybox timeout 300 /sbin/apk --cache-dir /var/cache/apk \
        --no-network -v add restic > /root/m226-restic-apk.log 2>&1; then
        record install PASS
    else
        record install FAIL
        /bin/busybox tail -n 15 /root/m226-restic-apk.log || :
        exit 1
    fi
    result=functional
else
    /sbin/apk info -e restic >/dev/null 2>&1 || { record reopen FAIL; exit 1; }
    result=reopen
fi
if NAS_M226_BOOT_PHASE="$phase" /bin/busybox timeout 900 sh \
    /root/m226-restic-workload.sh "--$phase" \
    > "/root/m226-restic-$phase.log" 2>&1; then
    record "$result" PASS
else
    record "$result" FAIL
    /bin/busybox tail -n 35 "/root/m226-restic-$phase.log" || :
    exit 1
fi
sync
if [ "$phase" = first ]; then
    echo M226_FIRST_DISCOVERY_COMPLETE
else
    echo M226_REOPEN_DISCOVERY_COMPLETE
fi
