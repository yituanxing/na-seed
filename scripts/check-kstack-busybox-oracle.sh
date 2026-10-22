#!/bin/sh
# Real, unmodified upstream BusyBox ash/echo/wc on native Linux.
# This is an OS oracle, NOT a NaSeed acceptance result. The QEMU matrix
# runs equivalent shell scripts via /bin/busybox in the guest CPIO.
set -eu
binary=${1:-build/source-busybox/amd64/busybox}
test -x "$binary"
case "$binary" in /*) ;; *) binary="$PWD/$binary" ;; esac
first=$("$binary" sh -c 'i=0; while [ $i -lt 6 ]; do "$1" echo child-$i; i=$((i+1)); done; echo kstack-forks-ok' sh "$binary")
test "$(printf '%s\n' "$first" | grep -c '^child-[0-5]$')" -eq 6
test "$(printf '%s\n' "$first" | tail -n 1)" = kstack-forks-ok
second=$("$binary" sh -c 'i=0; while [ $i -lt 6 ]; do echo ping | "$1" wc -c; i=$((i+1)); done; echo kstack-pipes-ok' sh "$binary")
test "$(printf '%s\n' "$second" | grep -c '^5$')" -eq 6
test "$(printf '%s\n' "$second" | tail -n 1)" = kstack-pipes-ok
echo '[TASK STACK BUSYBOX LINUX ORACLE PASS] native ash fork + pipeline stress'
