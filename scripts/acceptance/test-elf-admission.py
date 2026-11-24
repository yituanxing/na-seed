#!/usr/bin/env python3
"""Host-only tests of actual admission/hash helpers (never APK/QEMU proof)."""
from pathlib import Path
import subprocess
import tempfile
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

    # Verify every stronger mode with real symlinks, executable permissions,
    # an outside-root target, a multicall alias, and ELF magic rejection.
    a, b = root / "bin/a", root / "bin/b"
    a.write_bytes(b"\x7fELFa")
    b.write_bytes(b"\x7fELFb")
    a.chmod(0o755)
    b.chmod(0o755)
    modes = ("alias-path", "alias-exec", "contained-exec",
             "contained-file", "contained-elf", "contained-exec-elf")
    for mode in modes:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd)
        assert output.read_text() == subprocess.check_output(
            ["sha256sum", str(a), str(b)], text=True)
    b.chmod(0o644)
    run(HASH, root, current, output, 2, "alias-path", "M999", cwd=cwd)
    for mode in modes[1:]:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.chmod(0o755)
    b.write_bytes(b"not ELF")
    run(HASH, root, current, output, 2, "contained-file", "M999", cwd=cwd)
    for mode in ("contained-elf", "contained-exec-elf"):
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.rename(root / "bin/busybox")
    b.symlink_to("busybox")
    for mode in modes:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.unlink()
    outside = cwd / "outside"
    outside.write_bytes(b"\x7fELFb")
    outside.chmod(0o755)
    b.symlink_to(outside)
    for mode in modes[2:]:
        run(HASH, root, current, output, 2, mode, "M999", cwd=cwd, status=1)
    b.unlink()
    real_b = root / "bin/real-b"
    real_b.write_bytes(b"\x7fELFb")
    real_b.chmod(0o755)
    b.symlink_to("real-b")
    run(HASH, root, current, output, 2, "alias-path", "M999", cwd=cwd)
    assert str(b) in output.read_text()
    run(HASH, root, current, output, 2, "contained-elf", "M999", cwd=cwd)
    assert str(real_b) in output.read_text()

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
        assert "scripts/" + name in call or ('"$manifest"' in call and "manifest=scripts/" + name in body)
        expected = re.search(r' (\d+) (count|unique|alias-path|alias-exec|contained-exec|contained-file|contained-elf|contained-exec-elf) M\d+$', call)
        assert expected and int(expected[1]) == len(entries), (script, call, len(entries))
    migrated += 1
# Verify shared staging with the original per-profile counts and hash policy.
shared = scripts / "stage-alpine-signed-batch.sh"
subprocess.run(["sh", "-n", str(shared)], check=True)
profiles = {}
for names, count, mode in re.findall(r"^    (m\d+(?:\|m\d+)*)\) count=(\d+); mode=([\w-]+) ;;$", shared.read_text(), re.M):
    for name in names.split("|"):
        assert name not in profiles
        profiles[name] = (int(count), mode)
assert profiles == {f"m{n}": (21 if n == 110 else 15, "count" if n <= 115 else "unique")
                    for n in range(109, 119)}
for profile, (count, mode) in profiles.items():
    entries = [x for x in manifests[profile + "-original-elfs.txt"] if x and not x.startswith("#")]
    assert len(entries) == count, (profile, count, len(entries))

# Host-only synthetic overlay; the production shared stage and hash helpers run.
with tempfile.TemporaryDirectory() as d:
    import shutil
    cwd = Path(d)
    (cwd / "scripts/acceptance").mkdir(parents=True)
    shutil.copyfile(HASH, cwd / "scripts/acceptance/hash-original-elfs.sh")
    shutil.copyfile(HISTORY, cwd / "scripts/acceptance/check-elf-history.sh")
    (cwd / "scripts/stage-alpine-prebuilt-overlay.sh").write_text("#!/bin/sh\nexit 0\n")
    for profile, (count, mode) in profiles.items():
        work = cwd / "build/external" / (profile + "-batch") / "amd64"
        root = work / "rootfs"
        (root / "lib/apk/db").mkdir(parents=True)
        (root / "lib/apk/db/installed").write_text("P:fixture\n")
        (work / "rootfs.cpio").write_bytes(b"host fixture")
        rows = [f"usr/bin/{profile}-{i}" for i in range(count)]
        (root / "usr/bin").mkdir(parents=True)
        for row in rows:
            (root / row).write_bytes(row.encode())
        (cwd / "scripts" / (profile + "-original-elfs.txt")).write_text("\n".join(rows) + "\n")
        fixtures = {"m118": "m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so"}.get(profile, "").split()
        for fixture in fixtures:
            p = root / "tests" / fixture
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"fixture")
        run(shared, "amd64", profile, cwd=cwd)
        hashes = work / (profile + "-original-signed-elfs.sha256")
        assert len(hashes.read_text().splitlines()) == count
        if fixtures:
            (root / "tests" / fixtures[0]).unlink()
            run(shared, "amd64", profile, cwd=cwd, status=1)
    run(shared, "amd64", "m999", cwd=cwd, status=2)
    run(shared, "unknown", "m109", cwd=cwd, status=2)
migrated += len(profiles)
assert migrated >= 52, migrated
print(f"[ELF ADMISSION HOST PASS] {migrated} migrated scripts; exact path, count, hash and negative cases")
