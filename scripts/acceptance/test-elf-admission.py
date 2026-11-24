#!/usr/bin/env python3
"""Host-only tests of actual admission/hash helpers (never APK/QEMU proof)."""
from pathlib import Path
import subprocess
import tempfile
import hashlib
import re

ROOT = Path(__file__).resolve().parents[2]
DIR = ROOT / "scripts/acceptance"
HISTORY = DIR / "check-elf-history.sh"
HASH = DIR / "hash-original-elfs.sh"

def run(script, *args, cwd, status=0):
    p = subprocess.run(["sh", str(script), *map(str, args)], cwd=cwd,
                       text=True, capture_output=True)
    assert p.returncode == status, (script, args, p.returncode, p.stderr)
    return p.stdout

with tempfile.TemporaryDirectory() as d:
    cwd = Path(d)
    (cwd / "scripts").mkdir()
    current = Path("scripts/m999-original-elfs.txt")
    other = Path("scripts/m998-original-elfs.txt")
    (cwd / current).write_text("# comment\n\nbin/a\nbin/b\n")
    (cwd / other).write_text("bin/c\n")
    assert run(HISTORY, "M999", current, cwd=cwd) == ""
    assert run(HISTORY, "M999", current, "--count", cwd=cwd) == "2\n"
    assert run(HISTORY, "M999", current, "--count", "--distinct", cwd=cwd) == "2\n"
    (cwd / other).write_text("bin/b\n")
    run(HISTORY, "M999", current, cwd=cwd, status=1)
    (cwd / other).write_text("bin/c\n")
    (cwd / current).write_text("bin/a\nbin/a\n")
    assert run(HISTORY, "M999", current, "--count", cwd=cwd) == "2\n"
    run(HISTORY, "M999", current, "--count", "--distinct", cwd=cwd, status=1)
    (cwd / current).write_text("bin/a\nbin/b\n")
    root = cwd / "root"
    (root / "bin").mkdir(parents=True)
    (root / "bin/a").write_bytes(b"a")
    (root / "bin/b").write_bytes(b"b")
    output = cwd / "sha"
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd)
    expected = subprocess.check_output(["sha256sum", str(root / "bin/a"),
                                        str(root / "bin/b")], text=True)
    assert output.read_text() == expected
    (root / "bin/b").write_bytes(b"a")
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd, status=1)
    run(HASH, root, current, output, 2, "count", "M999", cwd=cwd)
    run(HASH, root, current, output, 3, "count", "M999", cwd=cwd, status=1)
    (root / "bin/b").unlink()
    run(HASH, root, current, output, 2, "unique", "M999", cwd=cwd, status=1)

# Verify all migrated shell sources parse and reference the original manifests.
# Compare historical admission against the original exact-line rule using
# the real repository manifests, including any pre-existing overlaps.
scripts = ROOT / "scripts"
manifests = {p.name: p.read_text().splitlines()
             for p in scripts.glob("m[0-9]*-original-elfs.txt")}
migrated = 0
for script in sorted(scripts.glob("stage-m*-prebuilt.sh")):
    body = script.read_text()
    if "scripts/acceptance/check-elf-history.sh" not in body and "scripts/acceptance/hash-original-elfs.sh" not in body:
        continue
    subprocess.run(["sh", "-n", str(script)], check=True)
    number = re.fullmatch(r"stage-m(\d+)-prebuilt.sh", script.name)[1]
    name = f"m{number}-original-elfs.txt"
    entries = [x for x in manifests[name] if x and not x.startswith("#")]
    if "scripts/acceptance/check-elf-history.sh" in body:
        call = next(x for x in body.splitlines() if "scripts/acceptance/check-elf-history.sh" in x)
        conflict = any(x in rows for x in entries for k, rows in manifests.items() if k != name)
        distinct = "--distinct" in call
        conflict = conflict or (distinct and len(entries) != len(set(entries)))
        args = ["--count"] if "--count" in call else []
        if distinct: args.append("--distinct")
        result = run(HISTORY, f"M{number}", "scripts/" + name, *args, cwd=ROOT, status=int(conflict))
        if not conflict and args: assert result == f"{len(entries)}\n"
    if "scripts/acceptance/hash-original-elfs.sh" in body:
        call = next(x for x in body.splitlines() if "scripts/acceptance/hash-original-elfs.sh" in x)
        assert "scripts/" + name in call
        expected = re.search(r' (\d+) (count|unique) M\d+$', call)
        assert expected and int(expected[1]) == len(entries), (script, call, len(entries))
    migrated += 1
assert migrated >= 52, migrated
print(f"[ELF ADMISSION HOST PASS] {migrated} migrated scripts; exact path, count, hash and negative cases")
