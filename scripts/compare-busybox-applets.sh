#!/bin/sh
# Actual applet-level BusyBox CONFIG parity: two separately compiled ELF64s
# from one pinned Debian busybox-static package revision. Userspace QEMU
# executes the *original* RV64 binary; the amd64 binary runs natively.
# This detects distro/Kconfig applet drift before debugging kernel syscalls.
# A matching --list is NECESSARY, not proof of identical all CONFIG_* bits.
set -eu
command -v qemu-riscv64 >/dev/null 2>&1 || {
    echo '[BUSYBOX CONFIG] qemu-user (qemu-riscv64) required' >&2
    exit 2
}
test -s build/port-riscv64/full/busybox ||
    sh scripts/fetch-debian-riscv64.sh
x86=build/port-riscv64/full/compared-amd64/busybox
sh scripts/fetch-busybox-fixture.sh amd64 "$x86"
rv=build/port-riscv64/full/busybox
dir=build/port-riscv64/full/applet-parity
mkdir -p "$dir"
LC_ALL=C "$x86" --list > "$dir/x86.applets"
LC_ALL=C qemu-riscv64 "$rv" --list > "$dir/rv64.applets"
sort -u "$dir/x86.applets" > "$dir/x86.sorted"
sort -u "$dir/rv64.applets" > "$dir/rv64.sorted"
xcount=$(wc -l < "$dir/x86.sorted")
rcount=$(wc -l < "$dir/rv64.sorted")
echo "[BUSYBOX CONFIG] amd64=$xcount applets; riscv64=$rcount applets"
if ! diff -u "$dir/x86.sorted" "$dir/rv64.sorted"; then
    echo '[BUSYBOX CONFIG FAIL] ISA packages expose different applet sets.' >&2
    echo 'A version string match alone does not make these equivalent.' >&2
    exit 1
fi
# The 17-item parity workload must use applets present in BOTH binaries.
for cmd in echo uname pwd id true printf ls cat head grep wc sh mkdir rm rmdir; do
    if ! grep -Fxq "$cmd" "$dir/x86.sorted"; then
        echo "[BUSYBOX CONFIG FAIL] required applet absent: $cmd" >&2
        exit 1
    fi
done
echo "[PASS] matching actual BusyBox --list applet configurations across amd64 and RV64"
