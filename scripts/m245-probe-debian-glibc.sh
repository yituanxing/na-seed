#!/bin/sh
# M245: independently prove one original Debian 13 dynamic glibc ELF runs
# inside NaSeed x86-64 system-QEMU.  Neither host preparation nor the
# inherited static BusyBox harness's own PASS line constitutes this proof.
set -eu

if [ "$#" -ne 1 ] || [ "$1" != amd64 ]; then
    echo 'usage: sh scripts/m245-probe-debian-glibc.sh amd64' >&2
    exit 2
fi
for binary in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum xorriso; do
    command -v "$binary" >/dev/null 2>&1 || {
        echo "[M245 FAIL] missing verified-fixture/QEMU prerequisite $binary" >&2
        exit 1
    }
done

work=build/m245-debian-dynamic-glibc/amd64
mkdir -p "$work"
python3 scripts/m245-fetch-debian-dynamic.py --arch amd64 --out "$work/source"
source="$work/source/sysroot"
test -s "$work/source/manifest.json"
test -s "$source/usr/bin/printf"
test -s "$source/usr/lib/x86_64-linux-gnu/libc.so.6"
test -s "$source/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"

# Preserve a separate, proven static Debian shell to launch the original
# GNU coreutils ELF.  This is NOT the glibc test subject, and the separate
# serial oracle below requires the subsequent dynamic execve identity.
sh scripts/fetch-busybox-fixture.sh amd64 "$work/static-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/static-busybox" "$work"
root="$work/rootfs"
mkdir -p "$root/usr/bin" "$root/usr/lib/x86_64-linux-gnu" \
    "$root/lib" "$root/lib64"
cp "$source/usr/bin/printf" "$root/usr/bin/printf"
cp "$source/usr/lib/x86_64-linux-gnu/libc.so.6" \
    "$root/usr/lib/x86_64-linux-gnu/libc.so.6"
cp "$source/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
    "$root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
chmod 755 "$root/usr/bin/printf" \
    "$root/usr/lib/x86_64-linux-gnu/libc.so.6" \
    "$root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"

# Debian merged-/usr symlinks: both interpreter and libc lookup resolve to
# byte-for-byte original Debian ELF files, not to a musl library or shim.
ln -sfn ../usr/lib/x86_64-linux-gnu "$root/lib/x86_64-linux-gnu"
ln -sfn ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
    "$root/lib64/ld-linux-x86-64.so.2"
test "$(readlink "$root/lib64/ld-linux-x86-64.so.2")" = \
    ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
test "$(readlink "$root/lib/x86_64-linux-gnu")" = ../usr/lib/x86_64-linux-gnu

readelf -l "$root/usr/bin/printf" | \
    grep -Fq '[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]'
for relative in usr/bin/printf \
    usr/lib/x86_64-linux-gnu/libc.so.6 \
    usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2; do
    cmp "$source/$relative" "$root/$relative"
done

archive="$work/m245-debian-glibc.cpio"
(
    cd "$root"
    find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0
) > "$archive.tmp"
mv "$archive.tmp" "$archive"

# A second extraction inspects the *actual* boot archive, including the
# original program's and loader's bytes and the exact PT_INTERP link.
verify=$(mktemp -d "$work/verify-cpio.XXXXXX")
cpio -id --quiet -D "$verify" < "$archive"
for relative in usr/bin/printf \
    usr/lib/x86_64-linux-gnu/libc.so.6 \
    usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2; do
    cmp "$source/$relative" "$verify/$relative"
done
test "$(readlink "$verify/lib64/ld-linux-x86-64.so.2")" = \
    ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
test "$(readlink "$verify/lib/x86_64-linux-gnu")" = ../usr/lib/x86_64-linux-gnu

# The inherited parity harness grants 90s to m105-* cases; using its
# proven VFS-dynamic class avoids modifying or weakening shared CI code.
case_id=m105-m245-debian-glibc
case_file="$work/m245-cases.tsv"
args='sh -c '\''exec /usr/bin/printf "M245-DEBIAN-DYNAMIC-GLIBC-OK\n"'\'''
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio "$args" M245-DEBIAN-DYNAMIC-GLIBC-OK - /bin/busybox \
    > "$case_file"

echo '[M245 NATIVE GATE] preparing genuine x86-64 QEMU (no inherited static pass shortcut)'
NAS_ACCEPTANCE_CPIO="$archive" \
BUSYBOX_PARITY_BINARY="$work/static-busybox" \
BUSYBOX_PARITY_MANIFEST="$case_file" \
BUSYBOX_PARITY_CASE="$case_id" \
NAS_SYSCALL_TRACE=1 \
sh scripts/probe-x86-busybox-parity.sh

log=build/busybox-source-parity-x86/$case_id.clean.log
test -s "$log"
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/printf' "$log"
grep -Fq '[DYN] real ELF interpreter loaded, entry=' "$log"
grep -Fxq 'M245-DEBIAN-DYNAMIC-GLIBC-OK' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M245 FAIL] actual native NaSeed QEMU emitted kernel/application failure' >&2
    exit 1
fi
echo '[M245 ORIGINAL DEBIAN DYNAMIC GLIBC REAL X86 QEMU PASS] original printf, libc6 and ld-linux'
