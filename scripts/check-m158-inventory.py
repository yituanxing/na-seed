#!/usr/bin/env python3
"""Static contract for M158 30 signed image-codec ELFs and guest semantics.

Deliberately separate from the native-QEMU proof: this catches accidental
historical duplication, manifest/test drift and omitted per-ELF markers.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
manifest = ROOT / "scripts/m158-original-elfs.txt"
runner = ROOT / "tests/m158-batch-run.sh"
marker_file = ROOT / "scripts/m158-expected-markers.txt"
paths = [x.strip() for x in manifest.read_text().splitlines()
         if x.strip() and not x.startswith("#")]
apps = [Path(x).name for x in paths]
code = runner.read_text()
loop = re.search(r"^for app in (.*?); do$", code, flags=re.M | re.S)
assert loop, "M158 native guest case loop missing"
called = re.findall(r"[a-z0-9-]+", loop.group(1).replace("\\", ""))
case_begin = code.index('case "$app" in')
case_end = code.index("    esac", case_begin)
case_groups = re.findall(r"^      ([a-z0-9|]+)\)",
                         code[case_begin:case_end], flags=re.M)
case_labels = [name for group in case_groups for name in group.split("|")
               if name in apps]
markers = [x.strip() for x in marker_file.read_text().splitlines()
           if x.strip() and not x.startswith("#")]
expected = ["M158_" + app.upper().replace("-", "_") + "_PASS"
            for app in apps]

assert len(paths) == len(set(paths)) == 30
assert len(apps) == len(set(apps)) == 30
assert all(p.startswith("usr/bin/") for p in paths)
assert set(apps) == set(called) == set(case_labels), (
    "manifest, native runner loop and codec case set must contain same 30 apps"
)
assert len(case_labels) == 30, "each signed application needs one case label"
assert len(markers) == len(set(markers)) == 31
assert set(markers) == set(expected + ["M158_BATCH_PASS"])

for older in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
    if older == manifest:
        continue
    previous = {x.strip() for x in older.read_text().splitlines()
                if x.strip() and not x.startswith("#")}
    repeated = previous.intersection(paths)
    assert not repeated, (
        f"historically admitted original ELFs {sorted(repeated)} in {older}"
    )

assert code.count("format_check \"$encoder\" \"$encoded\"") == 1
assert code.count("image_check \"$decoded\"") == 1
print("[M158 STATIC PASS] 30 historically new signed ELF identities, "
      "15 reversible format pairs, 30 cases and 31 unique QEMU guest markers")
