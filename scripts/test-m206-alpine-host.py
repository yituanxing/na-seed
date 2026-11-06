#!/usr/bin/env python3
"""M206 zero-QEMU *negative* contracts for new original Alpine software.

These tests check manifests, genuine workload wiring, reporting fail-closed
semantics and a simulated physical-disk reader. They are not executable,
signed APK or guest compatibility evidence; those require the three native
QEMU installs plus independent restarts and host signed APK inventory.
"""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import re
import runpy
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parent.parent
CANDIDATES = ROOT / "scripts/m206-candidate-elfs.tsv"
COHORT = ROOT / "scripts/m206-alpine-cohort.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m206-candidates.txt"
REOPEN = ROOT / "scripts/m206-alpine-reopen-cases.tsv"
GUEST = ROOT / "tests/m206-software-workload.sh"
DISPATCH = ROOT / "tests/m205-software-workload.sh"
REPORTER = ROOT / "scripts/m206-compat-report.py"
DISK = ROOT / "scripts/m206-alpine-disk-check.py"


def records(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def manifest_contract() -> list[tuple[str, str, str, str]]:
    candidate_lines = CANDIDATES.read_text().splitlines()
    cohort_lines = COHORT.read_text().splitlines()
    header = "package\telf_path\tfamily\tprobe"
    assert candidate_lines[0] == cohort_lines[0] == header
    assert records(CANDIDATES) == records(COHORT), "signed census != guest cohort"
    entries = [tuple(line.split("\t")) for line in candidate_lines[1:]
               if line.strip() and not line.startswith("#")]
    assert len(entries) == 20, "M206 requires 20 accountable original programs"
    assert all(len(item) == 4 for item in entries), "malformed TSV"
    assert len({item[1] for item in entries}) == 20
    assert len({(item[3], item[1]) for item in entries}) == 20
    owners = [item[0] for item in entries]
    requested = records(PACKAGES)
    assert len(requested) == len(set(requested)), "repeated APK request"
    assert set(owners) == set(requested), "signed APK owner mismatch"

    known: set[str] = set()
    for old in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
        if old.name != "m206-original-elfs.txt":
            known.update(records(old))
    # M205 discover-only paths lack an m205-original-elfs.txt. They still
    # must never be misrepresented as new M206 program candidates.
    for old in (ROOT / "scripts/m205-candidate-elfs.tsv",):
        known.update(line.split("\t")[1] for line in records(old)[1:])
    assert not ({item[1] for item in entries} & known), "M206 repeats earlier ELF"

    for package, path, family, probe in entries:
        assert re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9.+_-]*", package), package
        assert re.fullmatch(r"[a-z][a-z0-9-]*", family), family
        assert re.fullmatch(r"[a-z][a-z0-9-]*", probe), probe
        assert path.startswith(("bin/", "sbin/", "usr/bin/", "usr/sbin/"))
        assert ".." not in Path(path).parts and not Path(path).is_absolute()
    return entries


def guest_contract(entries: list[tuple[str, str, str, str]]) -> None:
    # The proven M205 guest owns package accounting, fd9 manifest iteration,
    # errors, first and reopening; M206 only owns its original command probes.
    # This keeps a single trustworthy install/QEMU lifecycle implementation.
    dispatch = DISPATCH.read_text()
    assert "NAS_M206_COMPAT_COHORT" in dispatch
    assert "milestone=M206" in dispatch
    assert "exec 0</dev/null" in dispatch
    assert dispatch.count('done 9< "$manifest"') >= 2, "stdin theft regression"
    assert "'%s_RESULT\\t" in dispatch and "'%s_ERROR\\t" in dispatch
    assert "${milestone}_FIRST_DISCOVERY_COMPLETE" in dispatch
    assert "${milestone}_REOPEN_DISCOVERY_COMPLETE" in dispatch
    assert "m206-software-workload.sh" in dispatch
    # Reuse M205's executable fd9/theft-negative harness with the *new*
    # M206 manifest. This launches a child that drains stdin for all twenty
    # rows, proving neither APK pre-scan nor functional iteration loses an
    # original command when a child reads fd0.
    owner = runpy.run_path(str(ROOT / "scripts/test-m205-alpine-host.py"))
    loop_test = owner["check_guest_manifest_loop"]
    for fields in ("pkg path family check", "package elf family case_id"):
        loop_test(dispatch, DISPATCH, COHORT, 20, fields)
    script = GUEST.read_text()
    assert "--exercise" in script
    for _, path, _, probe in entries:
        # `case a:path|b:path)` is a legitimate shared fixture, while each
        # actual original program still needs its own exact invocation.
        label = f"{probe}:{path}"
        assert script.count(label) == 1, f"missing/ambiguous M206 native probe {label}"
        case = script.split(label, 1)[1].split("\n        ;;", 1)[0]
        assert "/" + path in case, f"semantic probe never invokes original ELF: {label}"
        assert "--help" not in case, f"help banner is not semantic acceptance: {label}"

    reopen = "\n".join(records(REOPEN))
    assert "NAS_M206_COMPAT_COHORT=1" in reopen
    assert "m205-software-workload.sh --reopen" in reopen
    assert "m197-software-workload.sh --reopen" in reopen
    assert "M197_PUBLIC_ALPINE_SOFTWARE_2026" in reopen
    subprocess.run(["sh", "-n", str(GUEST)], check=True, timeout=10)
    subprocess.run(["sh", "-n", str(DISPATCH)], check=True, timeout=10)


def uart(entries: list[tuple[str, str, str, str]], stage: str,
         changes: dict[tuple[str, str, str], str] | None = None) -> str:
    changes = changes or {}
    phases = ("install", "functional") if stage == "first" else ("reopen",)
    lines: list[str] = []
    for package, path, _, _ in entries:
        for phase in phases:
            status = changes.get((package, path, phase), "PASS")
            lines.append(f"M206_RESULT\t{package}\t{path}\t{phase}\t{status}")
    lines.append("M206_FIRST_DISCOVERY_COMPLETE" if stage == "first"
                 else "M206_REOPEN_DISCOVERY_COMPLETE")
    return "\n".join(lines) + "\n"


def reporting_contract(entries: list[tuple[str, str, str, str]]) -> None:
    with tempfile.TemporaryDirectory(prefix="m206-report-contract-") as tmp:
        base = Path(tmp)
        first, reopen, report = (base / "first", base / "reopen", base / "report.json")

        def call(*, strict: bool = True) -> tuple[int, dict | None, str]:
            args = [sys.executable, str(REPORTER), str(COHORT), str(first),
                    str(reopen), str(report)]
            if strict:
                args.append("--require-all")
            completed = subprocess.run(args, check=False, text=True,
                                       capture_output=True, timeout=10)
            return (completed.returncode,
                    json.loads(report.read_text()) if report.exists() else None,
                    completed.stdout + completed.stderr)

        clean_first, clean_reopen = uart(entries, "first"), uart(entries, "reopen")
        first.write_text(clean_first)
        reopen.write_text(clean_reopen)
        rc, data, logs = call()
        assert rc == 0, logs
        assert data is not None and data["milestone"] == "M206"
        assert data["total_entrypoints"] == 20
        assert data["newly_installed_full_pass_entrypoints"] == 20
        assert not data["independent_elf_sha_attested_here"]

        p, path, _, _ = entries[0]
        first.write_text(uart(entries, "first", {(p, path, "install"): "FAIL",
                                                 (p, path, "functional"): "BLOCKED"}))
        reopen.write_text(uart(entries, "reopen", {(p, path, "reopen"): "BLOCKED"}))
        rc, data, logs = call()
        assert rc != 0 and data is not None and path in data["failures"], logs
        assert data["newly_installed_full_pass_entrypoints"] == 19
        rc, _, _ = call(strict=False)
        assert rc == 0, "discovery mode must preserve genuine FAIL rather than disappear"

        first.write_text(uart(entries, "first", {(p, path, "install"): "PREEXISTING"}))
        reopen.write_text(clean_reopen)
        rc, data, _ = call()
        assert rc != 0 and data is not None and data["preexisting"] == 1

        first.write_text(clean_first.replace(f"M206_RESULT\t{p}\t{path}\tinstall\tPASS\n", ""))
        rc, _, _ = call()
        assert rc != 0, "missing native-ELF result accepted"
        first.write_text(clean_first + f"M206_RESULT\t{p}\t{path}\tinstall\tPASS\n")
        rc, _, _ = call()
        assert rc != 0, "duplicated native-ELF result accepted"
        first.write_text(clean_first.replace("\tfunctional\tPASS", "\tfunctional\tSKIPPED", 1))
        rc, _, _ = call()
        assert rc != 0, "SKIPPED cannot be accepted as PASS"
        first.write_text(clean_first)
        reopen.write_text(clean_reopen.replace("M206_REOPEN_DISCOVERY_COMPLETE", ""))
        rc, _, _ = call()
        assert rc != 0, "missing second genuine QEMU marker accepted"


def physical_disk_oracle_contract(entries: list[tuple[str, str, str, str]]) -> None:
    """Drive the actual M206 disk checker over simulated debugfs bytes.

    This tests the checker itself; real QEMU evidence comes only when the
    checker runs against the actual ext4 disk produced by the second boot.
    """
    module = runpy.run_path(str(DISK))
    verify = module["verify"]
    original_load = verify.__globals__["load"]
    try:
        with tempfile.TemporaryDirectory(prefix="m206-host-disk-negative-") as tmp:
            base = Path(tmp)
            signed = base / "signed"
            signed.mkdir()
            disk = base / "root.img"
            disk.write_bytes(b"test-only")
            sample = b"\x7fELF\x02\x01" + bytes(12) + struct.pack("<H", 62)
            blobs = {"/etc/apk/world": ("alpine-base\nopenrc\n" +
                         "\n".join(records(PACKAGES)) + "\n").encode(),
                     "/lib/apk/db/installed": ("\n".join("P:" + name + "\n" for name in records(PACKAGES)) + "\n").encode()}
            for idx, (_, path, _, _) in enumerate(entries):
                blobs["/" + path] = sample + bytes([idx])

            m191 = SimpleNamespace(
                ARCH_MACHINE={"amd64": 62, "riscv64": 243, "arm64": 183},
                guest_path=lambda root, path: root / path.lstrip("/"),
                extract_partition=lambda src, dst: dst.write_bytes(b"dummy ext4"),
                debugfs_cat=lambda part, path: blobs.get(path, b""),
            )
            def existing(name: str, alias: str):
                assert name == "m197-alpine-disk-check.py"
                return SimpleNamespace(verify=lambda *args: None,
                     load=lambda *args: SimpleNamespace(load_m191=lambda: m191))
            verify.__globals__["load"] = existing
            # Suppress the real disk oracle's user-facing PASS in this
            # simulated host-only negative test. It is not a QEMU result.
            with redirect_stdout(io.StringIO()):
                verify("amd64", signed, disk)

            saved = blobs["/etc/apk/world"]
            blobs["/etc/apk/world"] = b"alpine-base\nopenrc\n"
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "APK world missing" in str(exc)
            else:
                raise AssertionError("disk oracle accepted missing APK world package")
            blobs["/etc/apk/world"] = saved

            saved = blobs["/lib/apk/db/installed"]
            blobs["/lib/apk/db/installed"] = b"P:alpine-base\n"
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "installed database missing" in str(exc)
            else:
                raise AssertionError("disk oracle accepted missing APK installed DB")
            blobs["/lib/apk/db/installed"] = saved

            path = "/" + entries[0][1]
            saved = blobs[path]
            blobs[path] = b"not an ELF"
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "native ELF missing" in str(exc)
            else:
                raise AssertionError("disk oracle accepted synthetic non-ELF")
            blobs[path] = saved

            wrong_isa = bytearray(blobs[path])
            struct.pack_into("<H", wrong_isa, 18, 183)
            blobs[path] = bytes(wrong_isa)
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "wrong ELF ISA" in str(exc)
            else:
                raise AssertionError("disk oracle accepted another architecture's ELF")
            blobs[path] = saved

            other = "/" + entries[1][1]
            saved = blobs[other]
            blobs[other] = blobs[path]
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "repeated ELF SHA256" in str(exc)
            else:
                raise AssertionError("disk oracle accepted duplicate program SHA")
            blobs[other] = saved

            preseed = signed / entries[0][1]
            preseed.parent.mkdir(parents=True)
            preseed.write_bytes(b"forbidden host-preseed")
            try:
                verify("amd64", signed, disk)
            except ValueError as exc:
                assert "candidate preinstalled" in str(exc)
            else:
                raise AssertionError("disk oracle accepted a preseeded software binary")
    finally:
        verify.__globals__["load"] = original_load


def main() -> None:
    entries = manifest_contract()
    guest_contract(entries)
    reporting_contract(entries)
    physical_disk_oracle_contract(entries)
    print("[M206 HOST CONTRACT PASS] 20 new nonrepeating original command paths, "
          "20 semantic guest cases, fd9 input isolation, signed APK owner manifests, "
          "strict genuine first/reopen records, negative reporting and physical "
          "APK DB/20 independent ELF SHA anti-false-positive cases; "
          "no host run is a native QEMU software PASS")


if __name__ == "__main__":
    main()
