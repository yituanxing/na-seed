#!/bin/sh
# M248: one genuine native QEMU boot per ISA, with original Debian 13 glibc
# dash as the initial PID1 and four original Debian coreutils as real children.
# M245/M247's amd64 gate remains independent and unchanged.
set -eu

if [ "$#" -ne 1 ]; then
    echo 'usage: sh scripts/m248-probe-debian-dash-pid1.sh riscv64|arm64' >&2
    exit 2
fi
arch=$1
case "$arch" in
    riscv64)
        machine=RISC-V
        triplet=riscv64-linux-gnu
        loader=ld-linux-riscv64-lp64d.so.1
        interp=/lib/ld-linux-riscv64-lp64d.so.1
        label=RV64
        harness=scripts/probe-riscv64-busybox-matrix.sh
        logdir=build/port-riscv64/full/matrix-source
        ;;
    arm64)
        machine=AArch64
        triplet=aarch64-linux-gnu
        loader=ld-linux-aarch64.so.1
        interp=/lib/ld-linux-aarch64.so.1
        label=ARM64
        harness=scripts/probe-aarch64-busybox-matrix.sh
        logdir=build/port-aarch64/full/matrix-source
        ;;
    *)
        echo 'usage: sh scripts/m248-probe-debian-dash-pid1.sh riscv64|arm64' >&2
        exit 2
        ;;
esac

for tool in python3 curl apt-get apt-cache dpkg-deb readelf cpio cmp \
    sha256sum xorriso sgdisk mformat mmd mcopy; do
    command -v "$tool" >/dev/null || {
        echo "[M248 FAIL] missing $tool" >&2
        exit 1
    }
done

work="build/m248-debian-dash-pid1/$arch"
mkdir -p "$work"
# This host-only fetcher authenticates Debian's signed InRelease and exact
# Packages SHA256 before extracting the unmodified target ELF objects.  A
# host cross-ISA test is deliberately not counted as native guest execution.
python3 scripts/m248-fetch-debian-multiarch.py --arch "$arch" \
    --out "$work/source"
source=$(cd "$work/source/sysroot" && pwd -P)
manifest="$work/source/manifest.json"
test -s "$manifest"
python3 - "$manifest" "$arch" <<'PY'
import json
import sys
from pathlib import Path

data = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if data.get("architecture") != sys.argv[2]:
    raise SystemExit("[M248 FAIL] authenticated source manifest ISA mismatch")
if data.get("status") != "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS":
    raise SystemExit("[M248 FAIL] expected fail-closed original Debian source provenance")
PY

for name in dash true cat wc printf; do
    binary="$source/usr/bin/$name"
    # Debian may package a genuine ELF through its own merged-usr symlink.
    # Preserve the signed fetcher's inside-sysroot constraint on resolution.
    resolved=$(readlink -f "$binary")
    case "$resolved" in "$source"/*) : ;; *)
        echo "[M248 FAIL] $arch program escapes authenticated sysroot: $binary" >&2
        exit 1 ;; esac
    test -s "$binary" || {
        echo "[M248 FAIL] missing unmodified Debian $arch ELF: $binary" >&2
        exit 1
    }
    readelf -h "$binary" | grep -Fq "$machine"
    readelf -h "$binary" | grep -Eq 'Type:[[:space:]]+DYN '
    readelf -l "$binary" | grep -Fq \
        "[Requesting program interpreter: $interp]"
    needed=$(readelf -d "$binary" | \
        sed -n 's/.*(NEEDED).*Shared library: \[\([^]]*\)\].*/\1/p')
    if [ "$needed" != libc.so.6 ]; then
        echo "[M248 FAIL] unreviewed DT_NEEDED for $name: $needed" >&2
        exit 1
    fi
done
for lib in libc.so.6 "$loader"; do
    binary="$source/usr/lib/$triplet/$lib"
    resolved=$(readlink -f "$binary")
    case "$resolved" in "$source"/*) : ;; *)
        echo "[M248 FAIL] $arch library escapes authenticated sysroot: $binary" >&2
        exit 1 ;; esac
    test -s "$binary" || {
        echo "[M248 FAIL] missing original Debian $arch glibc ELF: $binary" >&2
        exit 1
    }
    readelf -h "$binary" | grep -Fq "$machine"
done

# Static native BusyBox only supplies the common, verified CPIO fixture and
# a required harness input.  It is never the guest PID1 or test subject.
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/static-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/static-busybox" "$work"
root="$work/rootfs"
mkdir -p "$root/usr/bin" "$root/usr/lib/$triplet" \
    "$root/lib" "$root/etc"
for name in dash true cat wc printf; do
    cp -L "$source/usr/bin/$name" "$root/usr/bin/$name"
    chmod 755 "$root/usr/bin/$name"
    cmp "$source/usr/bin/$name" "$root/usr/bin/$name"
done
for lib in libc.so.6 "$loader"; do
    cp -L "$source/usr/lib/$triplet/$lib" "$root/usr/lib/$triplet/$lib"
    chmod 755 "$root/usr/lib/$triplet/$lib"
    cmp "$source/usr/lib/$triplet/$lib" "$root/usr/lib/$triplet/$lib"
done
# Debian trixie's merged-/usr layout: PT_INTERP still requests /lib/ld-*,
# while the authenticated libc6 payload lives below /usr/lib/$triplet.
ln -sfn "../usr/lib/$triplet" "$root/lib/$triplet"
ln -sfn "../usr/lib/$triplet/$loader" "$root/lib/$loader"

printf '%s\n' M248-CAT-OK > "$root/etc/m248-input"
test "$(wc -c < "$root/etc/m248-input")" -eq 12
case "$arch" in
    riscv64) marker=M248-DASH-PID1-RV64-OK ;;
    arm64) marker=M248-DASH-PID1-ARM64-OK ;;
esac
printf '%s\n' \
    '#!/usr/bin/dash' \
    'set -eu' \
    'value=$(/usr/bin/cat /etc/m248-input)' \
    '[ "$value" = M248-CAT-OK ]' \
    'count=$(/usr/bin/wc -c < /etc/m248-input)' \
    '[ "$count" = 12 ]' \
    '/usr/bin/true > /dev/null' \
    '/usr/bin/cat /etc/m248-input' \
    '/usr/bin/wc -c /etc/m248-input' \
    '/usr/bin/cat /etc/m248-input | /usr/bin/wc -c &' \
    'pipe_pid=$!' \
    'wait "$pipe_pid"' \
    "exec /usr/bin/printf '$marker\\n'" \
    > "$root/etc/m248-case.sh"

# Verify the actual serialized archive, not just files in the staging tree.
archive="$work/m248-dash-pid1.cpio"
(cd "$root" && find . -print | LC_ALL=C sort | \
    cpio -o -H newc --quiet --owner=0:0) > "$archive.tmp"
mv "$archive.tmp" "$archive"
verify=$(mktemp -d "$work/verify-cpio.XXXXXX")
cpio -id --quiet -D "$verify" < "$archive"
for name in dash true cat wc printf; do
    cmp "$source/usr/bin/$name" "$verify/usr/bin/$name"
done
for lib in libc.so.6 "$loader"; do
    cmp "$source/usr/lib/$triplet/$lib" "$verify/usr/lib/$triplet/$lib"
done
cmp "$root/etc/m248-input" "$verify/etc/m248-input"
cmp "$root/etc/m248-case.sh" "$verify/etc/m248-case.sh"
test "$(readlink "$verify/lib/$triplet")" = "../usr/lib/$triplet"
test "$(readlink "$verify/lib/$loader")" = "../usr/lib/$triplet/$loader"

case_id="m105-m248-debian-dash-pid1-$arch"
case_file="$work/m248-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio /etc/m248-case.sh "$marker" - /usr/bin/dash \
    > "$case_file"

echo "[M248 NATIVE $label] Debian glibc dash direct PID1 + four coreutils in ONE QEMU"
NAS_ACCEPTANCE_CPIO="$archive" \
BUSYBOX_PARITY_BINARY="$work/static-busybox" \
BUSYBOX_PARITY_MANIFEST="$case_file" \
BUSYBOX_PARITY_CASE="$case_id" \
NAS_SYSCALL_TRACE=1 \
sh "$harness"

log="$logdir/$case_id.clean.log"
test -s "$log"
grep -Fq '[M5] CPIO executable selected: /usr/bin/dash' "$log"
grep -Fq '[CMDLINE] userspace argv from Limine: naseed.argv=/usr/bin/dash /etc/m248-case.sh' "$log"
for name in true cat wc printf; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$name" "$log"
done
dyn_count=$(grep -Fc '[DYN] real ELF interpreter loaded, entry=' "$log")
test "$dyn_count" -ge 5
grep -Fxq M248-CAT-OK "$log"
grep -Eq '^[[:space:]]*12[[:space:]]+/etc/m248-input$' "$log"
grep -Fxq 12 "$log"
grep -Fxq "$marker" "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
grep -Fq "[$label] userspace debug-exit status=0" "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo "[M248 FAIL] original Debian $arch dash guest failed" >&2
    exit 1
fi
echo "[M248 ORIGINAL DEBIAN DASH DIRECT PID1 REAL $label QEMU PASS] dash + true cat wc printf"
