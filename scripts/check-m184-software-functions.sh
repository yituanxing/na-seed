#!/bin/sh
# Static/host gate only. The actual signed Alpine ELF and writable /tmp
# machine-ID semantics must run inside real NaSeed on all three ISAs.
set -eu
sh scripts/m174-software-check-manifest.sh
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
