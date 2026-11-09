#!/bin/sh
# M247: an original signed Debian glibc dash is the initial PID1, not BusyBox.
# Its CPIO-resident script executes original Debian coreutils in one QEMU boot.
set -eu

if [ "$#" -ne 1 ] || [ "$1" != amd64 ]; then
    echo 'usage: sh scripts/m247-probe-debian-dash-pid1.sh amd64' >&2
    exit 2
fi
for tool in python3 apt-get apt-cache dpkg-deb readelf cpio cmp xorriso; do
    command -v "$tool" >/dev/null || {
        echo "[M247 FAIL] missing $tool" >&2; exit 1;
    }
done
work=build/m247-debian-dash-pid1/amd64
mkdir -p "$work"
python3 scripts/m245-fetch-debian-dynamic.py --arch amd64 --out "$work/source"
python3 scripts/m247-fetch-debian-dash.py \
    --arch amd64 --m245-out "$work/source" --out "$work/dash"
source="$work/source/sysroot"
dash="$work/dash/sysroot/usr/bin/dash"
test -s "$work/dash/manifest.json"
test -s "$dash"

# The pre-existing static fixture is used solely to prepare a canonical
# CPIO tree and to satisfy the generic parity harness copy requirement.
# The guest entry is explicitly /usr/bin/dash, never /bin/busybox.
sh scripts/fetch-busybox-fixture.sh amd64 "$work/static-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/static-busybox" "$work"
root="$work/rootfs"
mkdir -p "$root/usr/bin" "$root/usr/lib/x86_64-linux-gnu" \
    "$root/lib" "$root/lib64" "$root/etc"
cp "$dash" "$root/usr/bin/dash"
chmod 755 "$root/usr/bin/dash"
cmp "$dash" "$root/usr/bin/dash"
for program in true cat wc printf; do
    binary="$source/usr/bin/$program"
    test -f "$binary" && test ! -L "$binary"
    readelf -h "$binary" | grep -Fq 'Advanced Micro Devices X86-64'
    readelf -h "$binary" | grep -Eq 'Type:[[:space:]]+DYN '
    readelf -l "$binary" | grep -Fq \
        '[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]'
    needed=$(readelf -d "$binary" | \
        sed -n 's/.*(NEEDED).*Shared library: \[\([^]]*\)\].*/\1/p')
    test "$needed" = libc.so.6 || {
        echo "[M247 FAIL] unreviewed dependency for $program: $needed" >&2
        exit 1
    }
    cp "$binary" "$root/usr/bin/$program"
    chmod 755 "$root/usr/bin/$program"
    cmp "$binary" "$root/usr/bin/$program"
done
for lib in libc.so.6 ld-linux-x86-64.so.2; do
    cp "$source/usr/lib/x86_64-linux-gnu/$lib" \
        "$root/usr/lib/x86_64-linux-gnu/$lib"
    cmp "$source/usr/lib/x86_64-linux-gnu/$lib" \
        "$root/usr/lib/x86_64-linux-gnu/$lib"
done
chmod 755 "$root/usr/lib/x86_64-linux-gnu/"*
ln -sfn ../usr/lib/x86_64-linux-gnu "$root/lib/x86_64-linux-gnu"
ln -sfn ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
    "$root/lib64/ld-linux-x86-64.so.2"

# A true dash shell script bypasses the Limine command-line escaping
# constraints: substitutions, redirections, background pipelines and wait
# are interpreted by the original guest shell, not the harness.
printf '%s\n' M247-CAT-OK > "$root/etc/m247-input"
test "$(wc -c < "$root/etc/m247-input")" -eq 12
printf '%s\n' \
    '#!/usr/bin/dash' \
    'set -eu' \
    'value=$(/usr/bin/cat /etc/m247-input)' \
    '[ "$value" = M247-CAT-OK ]' \
    'count=$(/usr/bin/wc -c < /etc/m247-input)' \
    '[ "$count" = 12 ]' \
    '/usr/bin/true > /dev/null' \
    '/usr/bin/cat /etc/m247-input' \
    '/usr/bin/wc -c /etc/m247-input' \
    '/usr/bin/cat /etc/m247-input | /usr/bin/wc -c &' \
    'pipe_pid=$!' \
    'wait "$pipe_pid"' \
    "exec /usr/bin/printf 'M247-DASH-PID1-OK\n'" \
    > "$root/etc/m247-case.sh"

archive="$work/m247-dash-pid1.cpio"
(cd "$root" && find . -print | LC_ALL=C sort | cpio -o -H newc --quiet --owner=0:0) \
    > "$archive.tmp"
mv "$archive.tmp" "$archive"
verify=$(mktemp -d "$work/verify-cpio.XXXXXX")
cpio -id --quiet -D "$verify" < "$archive"
cmp "$dash" "$verify/usr/bin/dash"
for program in true cat wc printf; do
    cmp "$source/usr/bin/$program" "$verify/usr/bin/$program"
done
for lib in libc.so.6 ld-linux-x86-64.so.2; do
    cmp "$source/usr/lib/x86_64-linux-gnu/$lib" \
        "$verify/usr/lib/x86_64-linux-gnu/$lib"
done
cmp "$root/etc/m247-input" "$verify/etc/m247-input"
cmp "$root/etc/m247-case.sh" "$verify/etc/m247-case.sh"
test "$(readlink "$verify/lib/x86_64-linux-gnu")" = ../usr/lib/x86_64-linux-gnu
test "$(readlink "$verify/lib64/ld-linux-x86-64.so.2")" = \
    ../usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2

case_id=m105-m247-debian-dash-pid1
case_file="$work/m247-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio /etc/m247-case.sh M247-DASH-PID1-OK - /usr/bin/dash \
    > "$case_file"

echo '[M247 NATIVE GATE] original signed Debian dash is direct NaSeed PID1'
NAS_ACCEPTANCE_CPIO="$archive" \
BUSYBOX_PARITY_BINARY="$work/static-busybox" \
BUSYBOX_PARITY_MANIFEST="$case_file" \
BUSYBOX_PARITY_CASE="$case_id" \
NAS_SYSCALL_TRACE=1 \
sh scripts/probe-x86-busybox-parity.sh

log="build/busybox-source-parity-x86/$case_id.clean.log"
test -s "$log"
grep -Fq '[M5] CPIO executable selected: /usr/bin/dash' "$log"
grep -Fq '[CMDLINE] userspace argv from Limine: naseed.argv=/usr/bin/dash /etc/m247-case.sh' "$log"
for program in true cat wc printf; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$program" "$log"
done
dyn_count=$(grep -Fc '[DYN] real ELF interpreter loaded, entry=' "$log")
test "$dyn_count" -ge 5
grep -Fxq M247-CAT-OK "$log"
grep -Eq '^[[:space:]]*12[[:space:]]+/etc/m247-input$' "$log"
grep -Fxq 12 "$log"
grep -Fxq M247-DASH-PID1-OK "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M247 FAIL] original Debian dash guest failed' >&2
    exit 1
fi
echo '[M247 ORIGINAL DEBIAN DASH DIRECT PID1 REAL X86 QEMU PASS] dash + true cat wc printf'
