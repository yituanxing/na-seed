#!/usr/bin/env python3
"""Exercise the actual shared marker gate; no QEMU success is inferred."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts/acceptance/check-batch-markers.sh"
subprocess.run(["sh", "-n", CHECKER], check=True)
with tempfile.TemporaryDirectory() as tmp:
    manifest, log = Path(tmp) / "manifest", Path(tmp) / "serial"

    def check(entries, records, expected=2, status=0):
        manifest.write_text("\n".join(entries) + "\n")
        log.write_text("\n".join(records) + "\n")
        p = subprocess.run(["sh", CHECKER, "M999", "amd64", manifest, log,
                            str(expected), "0"], capture_output=True, text=True)
        assert p.returncode == status, (entries, records, p.returncode, p.stderr)

    good = ["M999_ONE_PASS", "M999_TWO_PASS", "M999_BATCH_PASS"]
    check(["# comment", "", *good], good)
    check(good, good[1:], status=1)
    check(good, [*good, good[0]], status=1)
    check(good, [good[-1]], status=1)
    check([*good, good[0]], good, status=2)
    check(["M998_ONE_PASS", *good[1:]], good, status=2)
    check(good[:-1], good, status=1)
    check(good, good, expected=3, status=1)
    check(good, ["prefix " + good[0], *good[1:]], status=1)
    # Every migrated caller retains its own manifest and exact expected count.
    migrated = 0
    for script in sorted((ROOT / "scripts").glob("probe-m*-batch.sh")):
        body = script.read_text()
        if "sh scripts/acceptance/check-batch-markers.sh" not in body:
            continue
        subprocess.run(["sh", "-n", script], check=True)
        m = re.search(r'^sh scripts/acceptance/check-batch-markers\.sh (M\d+) "\$port" (\S+) "\$log" (\d+) (\d+)$', body, re.M)
        assert m, script
        prefix, source, expected, tail = m.groups()
        expected = int(expected)
        path = ROOT / "scripts" / (prefix.lower() + "-expected-markers.txt")
        if source != '"$manifest"':
            assert ROOT / source == path, script
        else:
            assert "manifest=scripts/" + path.name in body, script
        entries = [s for s in path.read_text().splitlines() if s and not s.startswith("#")]
        assert len(entries) == len(set(entries)) == expected + 1, (script, entries)
        assert entries.count(prefix + "_BATCH_PASS") == 1, script
        assert all(s.startswith(prefix + "_") and s.endswith("_PASS") for s in entries), script
        manifest.write_text(path.read_text())
        log.write_text("\n".join(entries) + "\n")
        subprocess.run(["sh", CHECKER, prefix, "amd64", manifest, log,
                        str(expected), "0"], check=True)
        migrated += 1
    assert migrated >= 37, migrated
print(f"[BATCH MARKER HOST PASS] {migrated} real manifests; duplicate, missing, malformed and summary-only rejection")
