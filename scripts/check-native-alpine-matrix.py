#!/usr/bin/env python3
"""Fail closed on any lost, duplicated or ISA-skipped authentic Linux workload.

All 17 M38-M49 plus M51-M55 probes remain in the full matrix for each native
ISA. The ordinary PR gate runs 12 source-free prebuilt/ABI workloads; five
original guest source-build cases remain in the opt-in full mode.
"""
from pathlib import Path
import re
import subprocess
import sys

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
    "m51": "foundation",
    "m52": "foundation",
    "m53": "foundation",
    "m54": "foundation",
    "m55": "foundation",
}
seen = {}
for milestone, suite, path in rows:
    if milestone in seen:
        raise SystemExit(f"duplicate milestone: {milestone}")
    if milestone not in expected or expected[milestone] != suite:
        raise SystemExit(f"unexpected milestone or suite: {milestone}/{suite}")
    if not re.fullmatch(r"scripts/probe-m(?:3[89]|4[0-9]|5[1-5])-[a-z0-9-]+\.sh", path):
        raise SystemExit(f"invalid original probe: {path}")
    if not (root / path).is_file():
        raise SystemExit(f"missing original probe: {path}")
    probe = (root / path).read_text()
    # Native port routing now has one owner. The common checker validates all
    # three real runner/serial paths and rejects a reintroduced private case.
    if "BUSYBOX_PARITY_MANIFEST=" not in probe:
        raise SystemExit(f"{path} bypasses canonical manifest")
    seen[milestone] = path
if set(seen) != set(expected):
    raise SystemExit(f"lost probes: {sorted(set(expected)-set(seen))}")
subprocess.run([sys.executable, str(root / "scripts/check-native-qemu-target.py")],
               cwd=root, check=True)
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
# Guard the two-track contract: no historical probe removed; none of the
# costly guest builds silently run on the normal, fast PR path.
batch = (root / "scripts/probe-alpine-batch.sh").read_text()
for mode in ("fast", "full"):
    if f"{mode}:foundation" not in batch or f"{mode}:parallel" not in batch:
        raise SystemExit(f"batch does not define required {mode} suite counts")
for milestone in ("m38", "m48", "m49", "m54", "m55"):
    if milestone not in batch:
        raise SystemExit(f"guest source-build gating missing {milestone}")
if "NAS_NATIVE_MODE" not in batch or "[M50 DEEP DEFERRED]" not in batch:
    raise SystemExit("fast/full gate owner missing from batch script")
if "NAS_NATIVE_MODE:" not in w or "inputs.mode" not in w or "default: full" not in w:
    raise SystemExit("CI cannot select complete original source-build mode")
if "fromJSON(" not in w or "foundation" not in w or "parallel" not in w:
    raise SystemExit("PR fast gate suite expansion missing")
for name in ("m56-native-busybox", "m57-native-sqlite"):
    workflow = (root / ".github/workflows" / f"{name}.yml").read_text()
    if "workflow_dispatch:" not in workflow or "pull_request:" in workflow:
        raise SystemExit(f"{name} must remain explicitly runnable, not auto guest-build on PR")
print(f"[M50 CI MATRIX PASS] {len(seen)} original workloads x 3 ISAs retained; "
      "12 prebuilt/ABI fast PR cases and 5 opt-in genuine guest source builds")
