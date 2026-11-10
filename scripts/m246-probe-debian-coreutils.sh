#!/bin/sh
# M246: four untouched Debian 13 glibc programs inside ONE real x86 NaSeed
# QEMU guest. The proven M245 single-printf acceptance remains independent.
set -eu

if [ "$#" -ne 1 ] || [ "$1" != amd64 ]; then
    echo 'usage: sh scripts/m246-probe-debian-coreutils.sh amd64' >&2
    exit 2
fi
for binary in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum xorriso; do
    command -v "$binary" >/dev/null 2>&1 || {
        echo "[M246 FAIL] missing prerequisite $binary" >&2
        exit 1
    }
done

work=build/m246-debian-coreutils/amd64
mkdir -p "$work"
# The M245 fetcher authenticates the original Debian InRelease and the
# exact-version coreutils/libc6 .deb SHA256 records. It extracts entire
# packages, not just printf, without installing host libraries.
python3 scripts/m245-fetch-debian-dynamic.py --arch amd64 --out "$work/source"
source="$work/source/sysroot"
test -s "$work/source/manifest.json"

programs='true cat wc printf'
interp='/lib64/ld-linux-x86-64.so.2'
for program in $programs; do
    binary="$source/usr/bin/$program"
    test -f "$binary" && test ! -L "$binary"
    readelf -h "$binary" | grep -Fq 'ELF64'
    readelf -h "$binary" | grep -Fq 'Advanced Micro Devices X86-64'
    readelf -h "$binary" | grep -Eq 'Type:[[:space:]]+DYN '
    readelf -l "$binary" | grep -Fq "[Requesting program interpreter: $interp]"
    needed=$(readelf -d "$binary" | \
        sed -n 's/.*(NEEDED).*Shared library: \[\([^]]*\)\].*/\1/p')
    if [ "$needed" != libc.so.6 ]; then
        echo "[M246 FAIL] unreviewed dynamic dependencies for $program: $needed" >&2
        exit 1
    fi
    echo "[M246 VERIFIED] official Debian glibc PIE $program (libc.so.6 only)"
done
test -s "$source/usr/lib/x86_64-linux-gnu/libc.so.6"
test -s "$source/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"

# Static Debian BusyBox only launches commands. Each absolute /usr/bin path
# necessarily executes the genuine, dynamically linked Debian program.
sh scripts/fetch-busybox-fixture.sh amd64 "$work/static-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/static-busybox" "$work"
root="$work/rootfs"
mkdir -p "$root/usr/bin" "$root/usr/lib/x86_64-linux-gnu" \
    "$root/lib" "$root/lib64" "$root/etc"
for program in $programs; do
    cp "$source/usr/bin/$program" "$root/usr/bin/$program"
    chmod 755 "$root/usr/bin/$program"
    cmp "$source/usr/bin/$program" "$root/usr/bin/$program"
done
cp "$source/usr/lib/x86_64-linux-gnu/libc.so.6" \
    "$root/usr/lib/x86_64-linux-gnu/libc.so.6"
cp "$source/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
    "$root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
chmod 755 "$root/usr/lib/x86_64-linux-gnu/libc.so.6" \
    "$root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
ln -sfn ../usr/lib/x86_64-linux-gnu "$root/lib/x86_64-linux-gnu"
ln -sfn ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
    "$root/lib64/ld-linux-x86-64.so.2"
# Exactly twelve bytes; cat must reproduce it, wc must count it.
printf '%s\n' M246-CAT-OK > "$root/etc/m246-input"
test "$(wc -c < "$root/etc/m246-input")" -eq 12

archive="$work/m246-debian-coreutils.cpio"
(
    cd "$root"
    find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0
) > "$archive.tmp"
mv "$archive.tmp" "$archive"

# Verify the emitted boot archive, including symlinks and untouched ELF bytes.
verify=$(mktemp -d "$work/verify-cpio.XXXXXX")
cpio -id --quiet -D "$verify" < "$archive"
for program in $programs; do
    cmp "$source/usr/bin/$program" "$verify/usr/bin/$program"
done
for lib in libc.so.6 ld-linux-x86-64.so.2; do
    cmp "$source/usr/lib/x86_64-linux-gnu/$lib" \
        "$verify/usr/lib/x86_64-linux-gnu/$lib"
done
test "$(readlink "$verify/lib/x86_64-linux-gnu")" = ../usr/lib/x86_64-linux-gnu
test "$(readlink "$verify/lib64/ld-linux-x86-64.so.2")" = \
    ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
cmp "$root/etc/m246-input" "$verify/etc/m246-input"

# One busybox sh, one original Debian execution for each of true/cat/wc/
# printf, one QEMU boot. The kernel's Limine argv parser consumes one '\',
# hence '\\n' is required to deliver GNU printf a real newline escape.
case_id=m105-m246-debian-coreutils
case_file="$work/m246-cases.tsv"
args='sh -c '\''/usr/bin/true && /usr/bin/cat /etc/m246-input && /usr/bin/wc -c /etc/m246-input && exec /usr/bin/printf "M246-GLIBC-BATCH-OK\\n"'\'''
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio "$args" M246-GLIBC-BATCH-OK - /bin/busybox \
    > "$case_file"

echo '[M246 NATIVE GATE] four real Debian glibc programs in ONE x86 NaSeed QEMU'
NAS_ACCEPTANCE_CPIO="$archive" \
BUSYBOX_PARITY_BINARY="$work/static-busybox" \
BUSYBOX_PARITY_MANIFEST="$case_file" \
BUSYBOX_PARITY_CASE="$case_id" \
NAS_SYSCALL_TRACE=1 \
sh scripts/probe-x86-busybox-parity.sh

log="build/busybox-source-parity-x86/$case_id.clean.log"
test -s "$log"
for program in $programs; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$program" "$log"
done
# The static launcher has no PT_INTERP: these are the four genuine glibc
# loader transitions, not a host-side fixture or Linux reference execution.
dyn_count=$(grep -Fc '[DYN] real ELF interpreter loaded, entry=' "$log")
test "$dyn_count" -ge 4
grep -Fxq M246-CAT-OK "$log"
grep -Eq '^[[:space:]]*12[[:space:]]+/etc/m246-input$' "$log"
grep -Fxq M246-GLIBC-BATCH-OK "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M246 FAIL] native guest reported failure' >&2
    exit 1
fi
echo '[M246 ORIGINAL DEBIAN GLIBC COREUTILS REAL X86 QEMU PASS] true cat wc printf'
