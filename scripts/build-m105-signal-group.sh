#!/bin/sh
# Compile the same real static GNU/Linux C source on all three target ISAs.
# The source calls the kernel's actual musl-compatible clone ABI; neither
# a thread mock nor target-specific guest test hooks are involved.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-m105-signal-group.sh amd64|riscv64|arm64' >&2;exit 2; }
port=$1
case "$port" in
  amd64) cc=gcc;isa='Advanced Micro Devices X86-64' ;;
  riscv64) cc=riscv64-linux-gnu-gcc;isa='RISC-V' ;;
  arm64) cc=aarch64-linux-gnu-gcc;isa='AArch64' ;;
  *) echo "[M105 FAIL] unsupported ISA $port" >&2;exit 2 ;;
esac
out="build/source-busybox/$port/m105-signal-group"
mkdir -p "$(dirname "$out")"
command -v "$cc" >/dev/null
# Parent/fork/TTY paths retain stack canaries even in the static guest.
# Only raw clone worker_main has no TLS canary, by function attribute.
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    -fstack-protector-strong tests/m105-signal-group.c -o "$out"
readelf -h "$out" | grep -Fq "$isa"
if readelf -l "$out" | grep -Fq INTERP;then
    echo "[M105 FAIL] guest ELF contains PT_INTERP ($port)" >&2;exit 1
fi
if [ "$port" = amd64 ];then
    # Native Linux must first show the real zombie-leader thread group
    # survives leader SYS_exit then dies with actual signal wait status.
    (ulimit -c 0; timeout 30s "$out" --linux-oracle) > \
        "build/source-busybox/$port/m105-linux-oracle.log" || {
        rc=$?;echo "[M105 LINUX ORACLE FAIL] rc=$rc" >&2
        tail -80 "build/source-busybox/$port/m105-linux-oracle.log" >&2
        exit 1
    }
    grep -Fxq M105_LINUX_ORACLE_PASS \
        "build/source-busybox/$port/m105-linux-oracle.log"
    echo '[M105 LINUX ORACLE PASS] same original x86 ELF zombie-leader TGID'
fi
echo "[M105 TARGET BUILD PASS] $port genuine static Linux ELF"
