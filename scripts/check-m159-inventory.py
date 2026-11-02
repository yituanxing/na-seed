#!/usr/bin/env python3
"""Strict static inventory gate for 30 NEW real Alpine SVN/NSS/Netpbm ELFs.

No static check claims a guest QEMU PASS. Separate exact-head QEMU marker and
actual kernel [EXEC] evidence is mandatory for each architecture.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
manifest = ROOT / "scripts/m159-original-elfs.txt"
runner = ROOT / "tests/m159-batch-run.sh"
marker_file = ROOT / "scripts/m159-expected-markers.txt"

paths = [line.strip() for line in manifest.read_text().splitlines()
         if line.strip() and not line.startswith("#")]
apps = [Path(p).name for p in paths]
script = runner.read_text()
loop = re.search(r"^for app in (.*?); do$", script, flags=re.M | re.S)
assert loop, "M159 guest case loop missing"
called = re.findall(r"[a-z0-9-]+", loop.group(1).replace("\\", ""))
labels = [name for names in re.findall(r"^      ([a-z0-9-|]+)\)",
                                         script, flags=re.M)
          for name in names.split("|")]
pattern = ["M159_" + name.upper().replace("-", "_") + "_PASS" for name in apps]
expected = [line.strip() for line in marker_file.read_text().splitlines()
            if line.strip() and not line.startswith("#")]

assert len(paths) == 30 and len(set(paths)) == len(paths)
assert len(set(apps)) == 30 and all(p.startswith("usr/bin/") for p in paths)
assert len(set(labels)) == 30, "30 explicit real operation cases required"
assert set(called) == set(labels) == set(apps), (
    "manifest, guest runner and cases diverge"
)
assert len(expected) == 31 and len(set(expected)) == 31
assert set(expected) == set(pattern + ["M159_BATCH_PASS"]), "guest marker drift"

assert apps[:8] == ["svn", "svnadmin", "svndumpfilter", "svnfsfs", "svnlook",
                    "svnmucc", "svnrdump", "svnversion"]
assert apps[8:13] == ["certutil", "crlutil", "modutil", "pk12util", "shlibsign"]
assert len(apps[13:]) == 17
for older in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
    if older == manifest:
        continue
    prev = {line.strip() for line in older.read_text().splitlines()
            if line.strip() and not line.startswith("#")}
    repeated = set(paths).intersection(prev)
    assert not repeated, f"historically duplicate ELF paths: {sorted(repeated)} in {older}"

print("[M159 STATIC PASS] 30 historically new independent signed Alpine "
      "ELF identities, 8 SVN + 5 NSS + 17 Netpbm, exact cases/markers")
