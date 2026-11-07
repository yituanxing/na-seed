#!/bin/sh
# Genuine APK add + exact original ELF FTP semantic ledger; two independent
# boots share only the physical ext4 disk, not host-generated challenge bytes.
set -eu
case "${1:-}" in --first) phase=first ;; --reopen) phase=reopen ;; *) exit 2 ;; esac
test "$#" -eq 1 || exit 2
busy=/bin/busybox
manifest=/root/m225-cohort.tsv
test -s "$manifest" && test -x /sbin/apk && test -s /root/m225-ftp-workload.sh
ledger="/root/m225-$phase-results.tsv"
test ! -e "$ledger" || { echo '[M225 GUEST FAIL] stale phase ledger'; exit 1; }
touch "$ledger"
tab=$(printf '\t')

record() {
    printf 'M225_RESULT\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" | tee -a "$ledger"
}

failed=0
count=0
while IFS="$tab" read -r package elf family case_id || [ -n "$package" ]; do
    case "$package" in ''|'#'*|package) continue ;; esac
    count=$((count+1))
    case "$package:$elf:$case_id" in
        'lftp:usr/bin/lftp:original-host-bidirectional-transfer'|\
        'rclone:usr/bin/rclone:original-host-bidirectional-transfer') ;;
        *) echo '[M225 GUEST FAIL] unrecognized signed cohort manifest'; exit 2 ;;
    esac
    if [ "$phase" = first ]; then
        if /sbin/apk info -e "$package" >/dev/null 2>&1; then
            install=PREEXISTING
        elif "$busy" timeout 300 /sbin/apk --cache-dir /var/cache/apk --no-network \
                -v add "$package" > "/root/m225-$package-apk.log" 2>&1; then
            install=PASS
        else
            install=FAIL
        fi
        record "$package" "$elf" install "$install"
        if [ "$install" = FAIL ]; then
            record "$package" "$elf" functional BLOCKED
            failed=$((failed+1))
            continue
        fi
        result_phase=functional
    else
        result_phase=reopen
        if ! /sbin/apk info -e "$package" >/dev/null 2>&1; then
            record "$package" "$elf" reopen FAIL
            failed=$((failed+1))
            continue
        fi
    fi
    if NAS_M225_BOOT_PHASE="$phase" "$busy" timeout 180 sh \
            /root/m225-ftp-workload.sh --exercise "$case_id" "$elf" \
            > "/root/m225-$package-$phase.log" 2>&1; then
        record "$package" "$elf" "$result_phase" PASS
    else
        record "$package" "$elf" "$result_phase" FAIL
        printf 'M225_ERROR\t%s\t%s\t%s\tguest original FTP transfer failed\n' \
            "$package" "$elf" "$result_phase"
        "$busy" tail -n 12 "/root/m225-$package-$phase.log" || :
        failed=$((failed+1))
    fi
done < "$manifest"
test "$count" -eq 2 || { echo '[M225 GUEST FAIL] not exactly two originals'; exit 1; }
sync
if [ "$phase" = first ]; then
    echo M225_FIRST_DISCOVERY_COMPLETE
else
    echo M225_REOPEN_DISCOVERY_COMPLETE
fi
# Keep full FAIL evidence and complete the bounded inventory; the host's
# --require-all report plus independent host/PCAP/ext4 then refuses PASS.
echo "[M225 GUEST INVENTORY] phase=$phase failed=$failed originals=$count"
