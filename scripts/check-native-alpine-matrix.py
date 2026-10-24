#!/usr/bin/env python3
"""Fail closed on any lost, duplicated or ISA-skipped authentic Linux workload.

All 12 current M38-M49 probes must run on each of the 3 native ports. The
existing individual workflows remain manually runnable for fault isolation.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
rows = []
for lineno, line in enumerate((root / "scripts/native-alpine-matrix.tsv").read_text().splitlines(), 1):
    if not line or line.startswith("#"):
        continue
    fields = line.split("\t")
    if len(fields) != 3:
        raise SystemExit(f"matrix line {lineno}: expected 3 fields")
    milestone, suite, path = fields
    rows.append((milestone, suite, path))
expected = {
    **{f"m{i}": "foundation" for i in range(38, 42)},
    **{f"m{i}": "pthread" for i in range(42, 45)},
    **{f"m{i}": "lifecycle" for i in range(45, 48)},
    **{f"m{i}": "parallel" for i in range(48, 50)},
}
seen = {}
for milestone, suite, path in rows:
    if milestone in seen:
        raise SystemExit(f"duplicate milestone: {milestone}")
    if milestone not in expected or expected[milestone] != suite:
        raise SystemExit(f"unexpected milestone or suite: {milestone}/{suite}")
    if not re.fullmatch(r"scripts/probe-m(?:3[89]|4[0-9])-[a-z0-9-]+\.sh", path):
        raise SystemExit(f"invalid original probe: {path}")
    if not (root / path).is_file():
        raise SystemExit(f"missing original probe: {path}")
    probe = (root / path).read_text()
    for port in ("amd64", "riscv64", "arm64"):
        if not re.search(r"\b" + port + r"\)", probe):
            raise SystemExit(f"{path} does not accept native {port}")
    if "BUSYBOX_PARITY_MANIFEST=" not in probe:
        raise SystemExit(f"{path} bypasses canonical manifest")
    seen[milestone] = path
if set(seen) != set(expected):
    raise SystemExit(f"lost probes: {sorted(set(expected)-set(seen))}")
w = (root / ".github/workflows/m50-alpine-matrix.yml").read_text()
for suite in ("foundation", "pthread", "lifecycle", "parallel"):
    if suite not in w:
        raise SystemExit(f"new CI does not select suite: {suite}")
for port in ("amd64", "riscv64", "arm64"):
    if port not in w:
        raise SystemExit(f"new CI does not select port: {port}")
if "sh scripts/probe-alpine-batch.sh" not in w:
    raise SystemExit("new CI does not run shared batch entrypoint")
for path in seen.values():
    # Per-suite old workflow is manually addressable; it must not double-run
    # as an automatic PR workflow on rootfs/kernel changes.
    suffix = Path(path).name
    matching = list((root / ".github/workflows").glob("m*.yml"))
    if not any(suffix in wf.read_text() and "workflow_dispatch:" in wf.read_text()
               and "pull_request:" not in wf.read_text() for wf in matching):
        raise SystemExit(f"old standalone manual diagnostic unavailable: {path}")
print(f"[M50 CI MATRIX PASS] {len(seen)} original real Linux workloads x 3 ISAs, "
      "no omitted/duplicate profiles and opt-in focused workflows")
