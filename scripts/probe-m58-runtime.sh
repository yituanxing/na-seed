#!/bin/sh
# One native QEMU boot runs THREE original signed Alpine applications.
# No in-guest compiler, Make, CMake, Ninja or source tarballs are staged.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m58-runtime.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m58-prebuilt.sh "$port"
work="build/external/m58-runtime/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m58-runtime-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m58-runtime.clean.log"
test -s "$log"
for stage in \
    M58_ORIGINAL_SIGNED_BASH_PASS \
    M58_BASH_PIPELINE_PASS \
    M58_BASH_REDIRECTION_ARRAY_PASS \
    M58_BASH_FORK_WAIT_PASS \
    M58_BASH_TRAP_PASS \
    M58_TCL_FILE_PASS \
    M58_TCL_EXEC_PIPE_PASS \
    M58_TCL_TIMER_PASS \
    M58_TCL_FULL_PASS \
    M58_ORIGINAL_SIGNED_TCL_PASS \
    M58_ORIGINAL_SIGNED_OPENSSL_PASS \
    M58_OPENSSL_ENTROPY_PASS \
    M58_OPENSSL_DIGEST_PASS \
    M58_OPENSSL_ROUNDTRIP_PASS \
    M58_OPENSSL_CERT_VERIFY_PASS \
    M58_RUNTIME_FULL_PASS; do
    if ! grep -Fxq "$stage" "$log"; then
        echo "[M58 FAIL] $port: missing guest marker $stage" >&2
        tail -150 "$log" >&2
        exit 1
    fi
done
echo "[M58 PASS] $port genuine prebuilt Bash, Tcl and OpenSSL executed under NaSeed QEMU"
