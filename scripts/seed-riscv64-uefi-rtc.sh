#!/bin/sh
# Genuine UEFI VirtualRealTimeClockLib seed, using original firmware CODE.
# The variable's vendor GUID comes from RealTimeClockRuntimeDxe.inf, because
# VirtualRealTimeClockLib uses gEfiCallerIdGuid, NOT the global-variable GUID.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: seed-riscv64-uefi-rtc.sh IN_VARS OUT_VARS' >&2; exit 2; }
command -v virt-fw-vars >/dev/null || { echo '[RTC VARS FAIL] virt-fw-vars missing' >&2; exit 1; }
test -s "$1"
test "$1" != "$2"
utc=$(date -u +%s)
out_json="$2.seed.json"
python3 - "$out_json" "$utc" <<'PY'
import json
import struct
import sys
path, raw_epoch = sys.argv[1:]
epoch = int(raw_epoch)
assert 946684800 < epoch < 4102444800
with open(path, "w", encoding="utf-8") as f:
    json.dump({"variables": [{
        "name": "RtcEpochSeconds",
        "guid": "b336f62d-4135-4a55-ae4e-4971bbf0885d",
        "attr": 7, # NV | BOOTSERVICE_ACCESS | RUNTIME_ACCESS
        "data": struct.pack("<Q", epoch).hex(),
    }]}, f)
PY
virt-fw-vars --input "$1" --output "$2" --set-json "$out_json"
virt-fw-vars --input "$2" --output-json "$2.readback.json"
python3 - "$2.readback.json" "$utc" <<'PY'
import json
import struct
import sys
src, raw_epoch = sys.argv[1:]
variables = json.load(open(src, encoding="utf-8"))["variables"]
matches = [v for v in variables
           if v["name"] == "RtcEpochSeconds" and
           v["guid"].lower() == "b336f62d-4135-4a55-ae4e-4971bbf0885d"]
assert len(matches) == 1, "RtcEpochSeconds not present exactly once"
v = matches[0]
assert v["attr"] & 7 == 7
actual, = struct.unpack("<Q", bytes.fromhex(v["data"]))
assert actual == int(raw_epoch), (actual, raw_epoch)
print("[RTC VARS PASS] real UEFI variable round-trip UTC=%d" % actual)
PY
