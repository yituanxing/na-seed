#!/usr/bin/env python3
"""M215 pilot contract: five real M213 ELF cases; ZERO claimed QEMU PASS.

The old M197 runner remains the only guest PID1/QEMU owner, M205 the only
APK/ledger owner, and M206 the physical-disk oracle. This host test proves
the five-case selector never sweeps fifteen unsupported M213 candidates into
an invented success, and that first/reopen evidence rejects wrong ledgers.
"""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
PILOT = ROOT / "scripts/m215-candidate-elfs.tsv"
ALL = ROOT / "scripts/m213-candidate-elfs.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m215-candidates.txt"
PROBE = ROOT / "tests/m213-software-workload.sh"
OWNER = ROOT / "tests/m205-software-workload.sh"
REOPEN = ROOT / "scripts/m215-alpine-reopen-cases.tsv"
REPORTER = ROOT / "scripts/m215-compat-report.py"
DISK = ROOT / "scripts/m215-alpine-disk-check.py"
RUNNER = ROOT / "scripts/probe-m197-alpine-software-cohort.sh"
INVENTORY = ROOT / "scripts/inventory-m141-signed-apks.sh"
WORKFLOW = ROOT / ".github/workflows/m215-five-original-pilot.yml"


def active(path: Path) -> list[str]:
    return [line for line in path.read_text().splitlines()
            if line.strip() and not line.startswith("#")]


def rows(path: Path) -> list[tuple[str, str, str, str]]:
    lines = active(path)
    assert lines[0] == "package\telf_path\tfamily\tprobe"
    entries = [tuple(line.split("\t")) for line in lines[1:]]
    assert all(len(row) == 4 for row in entries)
    return entries  # type: ignore[return-value]


def manifest() -> list[tuple[str, str, str, str]]:
    pilot, full = rows(PILOT), rows(ALL)
    assert len(pilot) == 5 and len(set(row[1] for row in pilot)) == 5
    assert set(pilot).issubset(set(full)), "pilot includes invented original ELF"
    assert {row[0] for row in pilot} == set(active(PACKAGES)) == {
        "iproute2", "util-linux-misc"}, "pilot package closure broadened"
    original = PROBE.read_text()
    for _, elf, _, case in pilot:
        branch = original.split(f"{case}:{elf})", 1)[1].split(";;", 1)[0]
        assert "probe_rc=0" in branch and "[M213 BLOCKED]" not in branch
        assert f"/{elf}" in branch
    for _, elf, _, case in set(full) - set(pilot):
        branch = original.split(f"{case}:{elf})", 1)[1].split(";;", 1)[0]
        assert "[M213 BLOCKED]" in branch and "probe_rc=0" not in branch
    assert len(full) - len(pilot) == 15
    return pilot


def adapter_contract() -> None:
    dispatcher = OWNER.read_text()
    assert "NAS_M215_PILOT" in dispatcher and "milestone=M215" in dispatcher
    assert "manifest=/root/m215-cohort.tsv" in dispatcher
    assert "exercise_script=/root/m213-software-workload.sh" in dispatcher
    assert "expected_rows=5" in dispatcher and "cohort_rows" in dispatcher
    assert "exec 0</dev/null" in dispatcher and 'done 9< "$manifest"' in dispatcher
    base = RUNNER.read_text()
    assert 'm215=${NAS_M215_PILOT:-0}' in base
    assert 'm204+m205+m206+m215' in base, "overlapping modes must fail"
    for needle in (
        "cohort=m215", "packages-$cohort-candidates.txt", "signed-cache.sha256",
        "sha256sum -c", "m215-first-runner.log",
        "M215_FIRST_DISCOVERY_COMPLETE", "M215_REOPEN_DISCOVERY_COMPLETE",
        "scripts/m215-alpine-reopen-cases.tsv", "m215-compat-report.py",
        "m215-alpine-disk-check.py", "--require-all"):
        assert needle in base, needle
    assert "apk --cache-dir /var/cache/apk --no-network" in dispatcher
    assert "M197_APP_WORKLOAD_PASS" in base and "m196-public-wire-check.py" in base
    assert "m191-alpine-root-disk.py verify" in base
    assert "M215_PILOT=1" in (ROOT / "scripts/probe-m215-alpine-pilot.sh").read_text()
    reopen = "\n".join(active(REOPEN))
    for value in ("NAS_M215_PILOT=1", "m205-software-workload.sh --reopen",
                  "m197-software-workload.sh --reopen", "M197_PUBLIC_ALPINE_SOFTWARE_2026"):
        assert value in reopen
    assert "m215" in INVENTORY.read_text() and "m215" in (
        ROOT / "scripts/check-m141-candidates.py").read_text()
    assert "expected_count=5" in DISK.read_text() and "m206-alpine-disk-check.py" in DISK.read_text()
    workflow = WORKFLOW.read_text()
    assert "workflow_dispatch:" in workflow
    assert "  push:\n    paths:\n      - '.github/workflows/m215-five-original-pilot.yml'" in workflow
    assert "sh scripts/inventory-m141-signed-apks.sh amd64 m215" in workflow
    assert "sh scripts/probe-m215-alpine-pilot.sh amd64" in workflow
    assert "matrix:" not in workflow, "pilot must not multiply into three simultaneous long QEMUs"
    for script in (OWNER, RUNNER, REOPEN, PROBE, INVENTORY,
                   ROOT / "scripts/probe-m215-alpine-pilot.sh"):
        if script.suffix == ".sh":
            subprocess.run(["sh", "-n", str(script)], check=True, timeout=10)
    subprocess.run([sys.executable, "scripts/check-m141-candidates.py",
                    "--profile", "m215"], cwd=ROOT, check=True, timeout=10)


def report_contract(pilot: list[tuple[str, str, str, str]]) -> None:
    def uart(stage: str, *, alteration: str = "", omit: bool = False) -> str:
        lines = []
        for idx, (pkg, elf, _, _) in enumerate(pilot):
            if idx == 0 and omit:
                continue
            for phase in (("install", "functional") if stage == "first" else ("reopen",)):
                value = alteration if alteration and idx == 0 and phase in ("functional", "reopen") else "PASS"
                lines.append(f"M215_RESULT\t{pkg}\t{elf}\t{phase}\t{value}")
        lines.append("M215_FIRST_DISCOVERY_COMPLETE" if stage == "first"
                     else "M215_REOPEN_DISCOVERY_COMPLETE")
        return "\n".join(lines) + "\n"

    with tempfile.TemporaryDirectory(prefix="m215-five-original-host-") as tmp:
        tmp_path = Path(tmp)
        first, second, result = (tmp_path / "first", tmp_path / "second", tmp_path / "result.json")

        def run(strict: bool = True) -> tuple[int, dict, str]:
            args = [sys.executable, str(REPORTER), str(PILOT), str(first),
                    str(second), str(result)] + (["--require-all"] if strict else [])
            action = subprocess.run(args, capture_output=True, text=True, timeout=8)
            data = json.loads(result.read_text()) if result.is_file() else {}
            return action.returncode, data, action.stdout + action.stderr

        first.write_text(uart("first"))
        second.write_text(uart("second"))
        rc, data, log = run()
        assert rc == 0, log
        assert data["milestone"] == "M215"
        assert data["newly_installed_full_pass_entrypoints"] == 5
        assert data["acceptance"] == "strict-native-first-and-reopen-5-of-5"
        assert data["independent_elf_sha_attested_here"] is False
        first.write_text(uart("first", alteration="FAIL"))
        rc, _, _ = run()
        assert rc != 0, "failed original ELF must not be accepted"
        first.write_text(uart("first"))
        second.write_text(uart("second", alteration="FAIL"))
        rc, _, _ = run()
        assert rc != 0, "second-QEMU failure must not be accepted"
        second.write_text(uart("second", omit=True))
        rc, _, _ = run()
        assert rc != 0, "missing original guest record must reject"


def main() -> None:
    entries = manifest()
    adapter_contract()
    report_contract(entries)
    print("[M215 FIVE-ELF PILOT HOST PASS] exact M213 subset 5/20, two signed APK owners, "
          "shared M197 QEMU first/reopen, M205 ledger and M206 ext4 oracle wiring")
    print("[M215 ZERO QEMU CLAIMS] no guest/install/SHA/native boot has run")


if __name__ == "__main__":
    main()
