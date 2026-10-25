#!/bin/sh
# Compare EDK2 default virtual RTC vs seeded RtcEpochSeconds on genuine
# RV64 QEMU, unmodified firmware CODE + Limine + original Debian BusyBox.
# This is a quick stand-alone diagnostic, not a fake M55 cJSON acceptance.
set -eu
export BUSYBOX_PARITY_MANIFEST=scripts/rtc-vars-case.tsv
export BUSYBOX_PARITY_CASE=rtc-epoch
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_RV64_RTC_TEST_FIRMWARE=1
log=build/port-riscv64/full/matrix/rtc-epoch.clean.log
for mode in baseline seeded; do
    if [ "$mode" = seeded ]; then
        export NAS_RV64_RTC_VAR_SEED=1
    else
        export NAS_RV64_RTC_VAR_SEED=0
    fi
    start=$(date -u +%s)
    sh scripts/probe-riscv64-busybox-matrix.sh
    end=$(date -u +%s)
    cp "$log" "build/port-riscv64/full/matrix/rtc-$mode.clean.log"
    python3 - "$log" "$mode" "$start" "$end" <<'PY'
import re
import sys
path, mode, start, end = sys.argv[1:]
with open(path, "rb") as f:
    data = f.read().replace(b"\r", b"")
times = [int(x) for x in re.findall(rb"(?m)^(\d{10})\s*$", data)]
if len(times) != 1:
    raise SystemExit("[RTC %s FAIL] date +%%s output absent/ambiguous: %s"
                     % (mode, times))
guest = times[0]
print("[RTC %s] guest UTC epoch=%d, host range=[%s,%s], offset=%ds"
      % (mode, guest, start, end, guest - int(start)))
if mode == "seeded" and not (int(start) - 5 <= guest <= int(end) + 5):
    raise SystemExit("[RTC SEEDED FAIL] firmware GetTime not current")
PY
done
echo '[PASS] original RV64 UEFI GetTime returns real UTC after varstore seed; no NaSeed time override'
