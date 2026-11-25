#!/bin/sh
# Host-only cumulative signed-software semantic contracts. Guest execution is
# still proved independently by the existing native QEMU marker oracles.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in m184|m185|m186|m187) ;; *) exit 2 ;; esac
sh scripts/check-software-batch-manifest.sh m174
python3 scripts/check-prebuilt-profile-catalog.py
sh -n tests/m174-software-run.sh
sh -n scripts/probe-m174-batch.sh
test "$(grep -Fxc 'M184_SOFTWARE_DBUS_UUID_FILE_FUNCTION_PASS' \
    scripts/m174-expected-markers.txt)" -eq 1
test "$(grep -Fxc 'echo M184_SOFTWARE_DBUS_UUID_FILE_FUNCTION_PASS' \
    tests/m174-software-run.sh)" -eq 1
for contract in 'busy mkdir -p "$work"' \
                '/usr/bin/dbus-uuidgen --ensure="$machine_id"' \
                '/usr/bin/dbus-uuidgen --get="$machine_id"' \
                'busy cmp -s "$machine_id" "$work/dbus-machine-id-original"' \
                'busy grep -Fxq' \
                '[ "$bad_rc" -gt 0 ] && [ "$bad_rc" -lt 124 ]'; do
    grep -Fq "$contract" tests/m174-software-run.sh || {
        echo "[M184 SOFTWARE CONTRACT FAIL] missing: $contract" >&2
        exit 1
    }
done
grep -Fq 'tests/m174-software-run.sh' scripts/m174-software-rootfs-copies.tsv
echo '[M184 SOFTWARE HOST CONTRACT PASS] one rootfs/CPIO, 20 signed candidates unchanged; DBus machine-ID real-guest oracle staged'
[ "$1" != m184 ] || exit 0
test "$(grep -Fxc 'M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS' \
    scripts/m174-expected-markers.txt)" -eq 1
test "$(grep -Fxc 'echo M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS' \
    tests/m174-software-run.sh)" -eq 1
grep -Fq 'busy timeout 15 /bin/iostat -c 1 2' tests/m174-software-run.sh
grep -Fq 'reports!=2 || samples!=2' tests/m174-software-run.sh
grep -Fxq 'bin/iostat' scripts/m174-software-original-elfs.txt
echo '[M185 SOFTWARE HOST CONTRACT PASS] 20 signed Alpine ELFs unchanged; original iostat real-CPU two-sample guest oracle staged'
[ "$1" != m185 ] || exit 0
grep -Fq 'busy timeout 15 /usr/bin/pidstat -u -p SELF 1 2' tests/m174-software-run.sh
grep -Fq 'echo M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' tests/m174-software-run.sh
grep -Fxq 'M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' scripts/m174-expected-markers.txt
grep -Fxq 'usr/bin/pidstat' scripts/m174-software-original-elfs.txt
if [ "$(grep -Fxc 'M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS' scripts/m174-expected-markers.txt)" -ne 1 ]; then
    echo '[M186 SOFTWARE HOST FAIL] PIDSTAT marker not unique' >&2
    exit 1
fi
echo '[M186 SOFTWARE HOST CONTRACT PASS] 20 original signed ELFs unchanged; real per-task pidstat CPU sampling staged'
[ "$1" != m186 ] || exit 0
test -s tests/m187-software-functions.sh
sh -n tests/m187-software-functions.sh
grep -Fq 'tests/m187-software-functions.sh' scripts/m174-software-rootfs-copies.tsv
grep -Fq '/tests/m187-software-functions.sh' tests/m174-software-run.sh

for elf in usr/bin/sar usr/bin/sadf usr/bin/inotifywait; do
    grep -Fxq "$elf" scripts/m174-software-original-elfs.txt || {
        echo "[M187 SOFTWARE HOST FAIL] missing signed original: $elf" >&2
        exit 1
    }
done
test "$(grep -Ec '^[^#[:space:]]' scripts/m174-software-original-elfs.txt)" -eq 20

for marker in \
    M187_SOFTWARE_SAR_LIVE_CPU_FUNCTION_PASS \
    M187_SOFTWARE_SAR_BINARY_CAPTURE_FUNCTION_PASS \
    M187_SOFTWARE_SAR_BINARY_REPLAY_FUNCTION_PASS \
    M187_SOFTWARE_SADF_BINARY_DECODE_FUNCTION_PASS \
    M187_SOFTWARE_INOTIFY_CREATE_MODIFY_FUNCTION_PASS; do
    test "$(grep -Fxc "$marker" scripts/m174-expected-markers.txt)" -eq 1 || {
        echo "[M187 SOFTWARE HOST FAIL] expected marker missing or duplicated: $marker" >&2
        exit 1
    }
    test "$(grep -Fxc "echo $marker" tests/m187-software-functions.sh)" -eq 1 || {
        echo "[M187 SOFTWARE HOST FAIL] guest marker not unique: $marker" >&2
        exit 1
    }
done

grep -Fq '/usr/bin/sar -u 1 2' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sar -u -o "$capture" 1 2' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sar -u -f "$capture"' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sadf -d "$capture" -- -u' tests/m187-software-functions.sh
grep -Fq '/usr/bin/inotifywait -q -e create' tests/m187-software-functions.sh
grep -Fq '/usr/bin/inotifywait -q -e modify' tests/m187-software-functions.sh
echo '[M187 SOFTWARE HOST CONTRACT PASS] existing 20 signed ELFs; sar/sadc/sadf real binary roundtrip + inotify CREATE/MODIFY staged'
