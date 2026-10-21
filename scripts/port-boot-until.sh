#!/bin/sh
# Watch the actual serial/semihost log for ALL verified hardware milestones.
# QEMU deliberately halts after PASS, so a blind "timeout 90s" otherwise
# burns 90s per architecture on every CI run. We terminate the host process
# only when its guest serial output satisfies every requested assertion.
# Caller owns the QEMU child and must always kill/reap it after this returns.
set -eu
if [ "$#" -lt 4 ]; then
    echo 'usage: port-boot-until.sh log qemu-pid seconds marker...' >&2
    exit 2
fi
log=$1 pid=$2 seconds=$3
shift 3
case "$seconds" in *[!0-9]*|'') exit 2 ;; esac
start=$(date +%s)
while :; do
    if [ -s "$log" ] && grep -Fq '[FAIL]' "$log"; then
        echo '[PORT MONITOR] guest reported FAIL before completing assertions' >&2
        exit 1
    fi
    complete=1
    for required do
        if ! grep -Fq "$required" "$log" 2>/dev/null; then
            complete=0
            break
        fi
    done
    if [ "$complete" -eq 1 ]; then
        echo '[PORT MONITOR] all real guest PASS markers observed; stopping QEMU early'
        exit 0
    fi
    now=$(date +%s)
    if [ "$((now-start))" -ge "$seconds" ] ||
       ! kill -0 "$pid" 2>/dev/null; then
        echo '[PORT MONITOR] QEMU ended/timed out before all PASS markers' >&2
        exit 1
    fi
    sleep 1
done
