#!/bin/sh
# Stage the SAME Linux C ownership-race fixture used by the A014 host oracle
# into M42's existing original-Alpine pthread rootfs. Do not invent a second
# guest runner, native library, or per-ISA syscall implementation.
set -eu
test "$#" -eq 1 || { echo 'usage: stage-a014-blocked-readv-m42.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) cc=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64) cc=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64) cc=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) echo '[A014 STAGE FAIL] unsupported target ISA' >&2; exit 2 ;;
esac
command -v "$cc" >/dev/null
work="build/external/m42-pthread/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
test -s "$root/tests/run-pthread.sh"
out="$root/tests/a014-blocked-readv"

# This is an authentic native Linux ELF linked against the target GNU libc;
# assertions, pthreads, readv and dup2 execute under the real NaSeed kernel.
# The test source is identical to the host Linux oracle, not a kernel mock.
# RV64/ARM64 software QEMU needs more wall time than the host Linux oracle;
# the two adversarial races remain byte-for-byte identical.
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -pthread -no-pie \
    -DA014_WATCHDOG_SECONDS=30 \
    scripts/test-a014-linux-blocked-readv.c -o "$out"
readelf -h "$out" | grep -Fq "$machine"
if readelf -l "$out" | grep -Fq INTERP; then
    echo "[A014 STAGE FAIL] unexpected dynamic interpreter: $port" >&2
    exit 1
fi

# The signed-package rootfs was built by rootfs/alpine/build.sh before this
# call. Regenerate only its CPIO index to include the guest ownership fixture.
# The original APK binaries and all existing M42 tests remain unchanged.
(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            test "${#path}" -le 254 || {
                echo "[A014 STAGE FAIL] CPIO path too long: $path" >&2
                exit 1
            }
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[A014 STAGE PASS] $port exact Linux oracle C source, static native ELF, existing M42 CPIO"
