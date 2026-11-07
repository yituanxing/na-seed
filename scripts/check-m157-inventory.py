#!/usr/bin/env python3
"""Static M157 gate: exact 30 original ELF, semantic cases and marker parity.

Host-only: this rejects honest-but-broken manifest drift before expensive
native QEMU, without pretending static checks prove guest behavior.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
manifest = ROOT / "scripts/m157-original-elfs.txt"
runner = ROOT / "tests/m157-batch-run.sh"
marker_file = ROOT / "scripts/m157-expected-markers.txt"
paths = [x.strip() for x in manifest.read_text().splitlines()
         if x.strip() and not x.startswith("#")]
apps = [Path(x).name for x in paths]
text = runner.read_text()
loop = re.search(r"^for app in (.*?); do$", text, flags=re.M | re.S)
assert loop, "runner loop missing"
called = re.findall(r"[a-z0-9-]+", loop.group(1).replace("\\", ""))
case_labels = re.findall(r"^      ([a-z0-9-]+)\)", text, flags=re.M)
marks = [x.strip() for x in marker_file.read_text().splitlines()
         if x.strip() and not x.startswith("#")]
expected = ["M157_" + a.upper().replace("-", "_") + "_PASS" for a in apps]
assert len(apps) == 30 and len(set(apps)) == 30, "not 30 independent names"
assert len(set(paths)) == 30 and all(p.startswith("usr/bin/") for p in paths)
assert set(apps) == set(called) == set(case_labels), (
    "original ELF manifest, semantic runner and exact per-program case set diverge"
)
assert len(marks) == 31 and len(set(marks)) == 31
assert set(marks) == set(expected + ["M157_BATCH_PASS"]), "marker drift"

for older in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
    if older == manifest:
        continue
    prev = {x.strip() for x in older.read_text().splitlines()
            if x.strip() and not x.startswith("#")}
    repeated = prev.intersection(paths)
    assert not repeated, f"historically admitted ELF {sorted(repeated)} in {older}"
print("[M157 STATIC PASS] exact 30 historically new signed ELF identities, "
      "30 matched semantic cases and 31 unique guest markers")
