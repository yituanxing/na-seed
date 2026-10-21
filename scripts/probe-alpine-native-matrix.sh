#!/bin/sh
# Grow the real, SHA256-pinned Alpine BusyBox workload in *both* native ISAs.
# Every scenario independently boots the SAME full Task/ELF/VM/FS kernel
# and actual Alpine ET_DYN ELF + interpreter loaded from real CPIO.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-alpine-native-matrix.sh arm64|riscv64' >&2; exit 2; }
port=$1
ALPINE_NATIVE_CASE=echo ALPINE_NATIVE_ARGS='echo hello' ALPINE_NATIVE_EXPECT=hello \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=cat ALPINE_NATIVE_ARGS='cat /etc/hostname' ALPINE_NATIVE_EXPECT=naseed \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=ls ALPINE_NATIVE_ARGS='ls /bin' ALPINE_NATIVE_EXPECT=busybox \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=shell ALPINE_NATIVE_ARGS='sh -c "echo alpine-shell"' ALPINE_NATIVE_EXPECT=alpine-shell \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=exec ALPINE_NATIVE_ARGS='sh -c "/bin/busybox cat /etc/hostname; echo finished"' ALPINE_NATIVE_EXPECT=finished \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=pipe ALPINE_NATIVE_ARGS='sh -c "echo abc | /bin/busybox wc -c"' ALPINE_NATIVE_EXPECT=4 \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
ALPINE_NATIVE_CASE=tmpfs ALPINE_NATIVE_ARGS='sh -c "mkdir /tmp/native && echo hello > /tmp/native/file && cat /tmp/native/file && rm /tmp/native/file && rmdir /tmp/native"' ALPINE_NATIVE_EXPECT=hello \
    sh scripts/probe-alpine-native-dynamic.sh "$port"
echo "[ALPINE NATIVE MATRIX PASS] $port original Alpine BusyBox + musl: 7/7"
