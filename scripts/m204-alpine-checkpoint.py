#!/usr/bin/env python3
"""Opt-in, verified *post-second-clean-boot* M204 Alpine disk checkpoint.

This is a fast kernel/ABI regression input, NEVER a substitute for the
current-commit cold public-HTTPS installation and independent two-boot gate.
"""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


VERSION = 1
ALPINE = "v3.23"
SOURCES = (
    "scripts/probe-m197-alpine-software-cohort.sh",
    "scripts/probe-m204-alpine-compat-cohort.sh",
    "scripts/probe-m191-alpine-root.sh",
    "scripts/m191-alpine-root-disk.py",
    "scripts/m193-alpine-login-overlay.py",
    "scripts/m194-alpine-uart.py",
    "scripts/m197-alpine-stage.py",
    "scripts/m197-alpine-uart.py",
    "scripts/m204-alpine-uart.py",
    "scripts/m195-alpine-disk-check.py",
    "scripts/m196-public-wire-check.py",
    "scripts/m197-alpine-disk-check.py",
    "scripts/m204-compat-report.py",
    "scripts/m204-alpine-checkpoint.py",
    "scripts/m197-alpine-cases.tsv",
    "scripts/m197-alpine-reopen-cases.tsv",
    "scripts/m204-alpine-cohort.tsv",
    "scripts/m204-alpine-reopen-cases.tsv",
    "scripts/m204-alpine-replay-cases.tsv",
    "tests/m197-software-workload.sh",
    "tests/m204-software-workload.sh",
)
MARKERS = (
    "M197_APP_WORKLOAD_PASS",
    "M204_FIRST_DISCOVERY_COMPLETE",
)
REOPEN_MARKERS = (
    "M197_REOPEN_WORKLOAD_PASS",
    "M204_REOPEN_DISCOVERY_COMPLETE",
    "[LWEXT4 GUEST] clean upstream unmount and durable flush",
    "[EXIT] user status=0",
    "[PASS] cmdline userspace exit 0",
)
PORTS = {"amd64", "arm64", "riscv64"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_hashes(repo: Path) -> dict[str, str]:
    return {name: sha256(repo / name) for name in SOURCES}


def package_manifest(repo: Path) -> list[tuple[str, str]]:
    rows = []
    seen = set()
    for line in (repo / "scripts/m204-alpine-cohort.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        category, package, workload = line.split("\t")
        if package in seen or not category or not workload:
            raise ValueError("[M204 CHECKPOINT FAIL] duplicate or invalid manifest")
        rows.append((category, package))
        seen.add(package)
    if len(rows) != 12:
        raise ValueError("[M204 CHECKPOINT FAIL] expected exactly twelve APK entries")
    return rows


def validate_uart(repo: Path, serial: Path) -> list[str]:
    lines = serial.read_text(errors="replace").replace("\r", "").splitlines()
    actual = set(lines)
    for marker in MARKERS:
        if marker not in actual:
            raise ValueError("[M204 CHECKPOINT INELIGIBLE] missing genuine first-boot marker: " + marker)
    packages = [package for _, package in package_manifest(repo)]
    install_lines = [line.split("\t") for line in lines
                     if line.startswith("M204_RESULT\t")]
    for package in packages:
        matches = [fields for fields in install_lines if len(fields) == 5 and
                   fields[2] == package and fields[3] == "install"]
        if len(matches) != 1 or matches[0][4] != "PASS":
            raise ValueError("[M204 CHECKPOINT INELIGIBLE] APK install is not a single new PASS: " + package)
    return packages


def validate_reopen_uart(serial: Path, packages: list[str]) -> None:
    # The *second* full cold-QEMU boot, not the original Alpine PID1 shell,
    # supplies the required ext4 clean-unmount evidence for caching.
    lines = serial.read_text(errors="replace").replace("\r", "").splitlines()
    actual = set(lines)
    for marker in REOPEN_MARKERS:
        if marker not in actual:
            raise ValueError("[M204 CHECKPOINT INELIGIBLE] second QEMU lacks clean evidence: " + marker)
    for package in packages:
        records = [line.split("\t") for line in lines if
                   line.startswith("M204_RESULT\t")]
        matches = [row for row in records if len(row) == 5 and
                   row[2] == package and row[3] == "reopen"]
        if len(matches) != 1 or matches[0][4] not in {"PASS", "FAIL", "BLOCKED"}:
            raise ValueError("[M204 CHECKPOINT INELIGIBLE] incomplete actual second-QEMU record: " + package)


def disk_oracle(port: str, root: Path, disk: Path,
                packages: list[str], reopen_serial: Path) -> None:
    # Import the existing independent host oracle instead of using an in-guest
    # declaration of success as evidence. This also runs host e2fsck.
    module_path = Path(__file__).with_name("m197-alpine-disk-check.py")
    spec = importlib.util.spec_from_file_location("m204_checkpoint_m197_oracle", module_path)
    if spec is None or spec.loader is None:
        raise ValueError("[M204 CHECKPOINT FAIL] missing host disk oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.verify(port, root, disk)
    m195 = module.load("m195-alpine-disk-check.py", "m204_checkpoint_m195_oracle")
    m195.verify(port, root, disk, stage="M204 CHECKPOINT",
                witness_path="/root/m197-wan.txt", witness=module.WITNESS,
                packages=tuple(packages), executables=())
    m191 = m195.load_m191()
    with tempfile.TemporaryDirectory(prefix="m204-clean-reopen-ledger-") as workspace:
        partition = Path(workspace) / "root.ext4"
        m191.extract_partition(disk, partition)
        persisted = m191.debugfs_cat(partition, "/root/m204-reopen-results.tsv")
        uart = reopen_serial.read_bytes().replace(b"\r", b"")
        rows = [line for line in persisted.splitlines()
                if line.startswith(b"M204_RESULT\t")]
        if len(rows) != len(packages) or any(
                line not in uart.splitlines() for line in rows):
            raise ValueError("[M204 CHECKPOINT FAIL] second-QEMU ext4 ledger differs from live UART")


def safe_file(path: Path) -> None:
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        raise ValueError("[M204 CHECKPOINT FAIL] absent/unsafe evidence: " + str(path))


def verify(port: str, root: Path, disk: Path, meta: Path,
           serial: Path, pcap: Path, reopen_serial: Path, repo: Path,
           *, check_disk=True) -> dict:
    if port not in PORTS:
        raise ValueError("[M204 CHECKPOINT FAIL] wrong ISA")
    for path in (root, disk, meta, serial, pcap, reopen_serial):
        if path.is_symlink():
            raise ValueError("[M204 CHECKPOINT FAIL] symlink evidence")
    for path in (disk, meta, serial, pcap, reopen_serial):
        safe_file(path)
    data = json.loads(meta.read_text())
    if (data.get("version") != VERSION or data.get("alpine") != ALPINE or
            data.get("port") != port or data.get("source_hashes") != source_hashes(repo) or
            not re.fullmatch(r"[0-9a-f]{40}", data.get("origin_commit", ""))):
        raise ValueError("[M204 CHECKPOINT FAIL] source/architecture provenance mismatch")
    if data.get("checkpoint_stage") != "post-independent-clean-reopen":
        raise ValueError("[M204 CHECKPOINT FAIL] no proven independent clean second QEMU")
    if data.get("sha256") != {"disk": sha256(disk), "serial": sha256(serial),
                               "pcap": sha256(pcap), "reopen_serial": sha256(reopen_serial)}:
        raise ValueError("[M204 CHECKPOINT FAIL] immutable evidence hash mismatch")
    if disk.stat().st_size != 256 * 1024 * 1024:
        raise ValueError("[M204 CHECKPOINT FAIL] unexpected physical disk size")
    packages = validate_uart(repo, serial)
    validate_reopen_uart(reopen_serial, packages)
    if data.get("packages") != packages:
        raise ValueError("[M204 CHECKPOINT FAIL] package manifest mismatch")
    if check_disk:
        disk_oracle(port, root, disk, packages, reopen_serial)
    print(f"[M204 VERIFIED CHECKPOINT] {port} post-second-clean-boot disk, "
          f"first/reopen UART and PCAP SHA256, twelve genuine APK installs, ext4 oracle; "
          f"origin={data['origin_commit']}")
    return data


def copy_public(path: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=destination.name + ".", suffix=".tmp",
                                     dir=destination.parent, delete=False) as stream:
        tmp = Path(stream.name)
    try:
        shutil.copyfile(path, tmp)
        os.chmod(tmp, 0o644)
        os.replace(tmp, destination)
    finally:
        if tmp.exists():
            tmp.unlink()


def capture(port: str, root: Path, disk: Path, serial: Path, pcap: Path,
            reopen_serial: Path,
            target_disk: Path, target_meta: Path, target_serial: Path,
            target_pcap: Path, target_reopen_serial: Path, repo: Path) -> None:
    for path in (root, disk, serial, pcap, reopen_serial):
        if path.is_symlink():
            raise ValueError("[M204 CHECKPOINT FAIL] original evidence symlink")
    for path in (disk, serial, pcap, reopen_serial):
        safe_file(path)
    try:
        packages = validate_uart(repo, serial)
        validate_reopen_uart(reopen_serial, packages)
    except ValueError as exc:
        if str(exc).startswith("[M204 CHECKPOINT INELIGIBLE]"):
            print(str(exc) + "; saving no replay cache")
            return
        raise
    disk_oracle(port, root, disk, packages, reopen_serial)
    # The metadata is committed LAST; no interrupted copy may be considered
    # a complete checkpoint. Do not remove a pre-existing valid checkpoint.
    copy_public(disk, target_disk)
    copy_public(serial, target_serial)
    copy_public(pcap, target_pcap)
    copy_public(reopen_serial, target_reopen_serial)
    origin = os.environ.get("GITHUB_SHA", "")
    if not re.fullmatch(r"[0-9a-f]{40}", origin):
        raise ValueError("[M204 CHECKPOINT FAIL] unverified GitHub origin commit")
    info = {"version": VERSION, "alpine": ALPINE, "port": port,
            "checkpoint_stage": "post-independent-clean-reopen",
            "origin_commit": origin, "source_hashes": source_hashes(repo),
            "packages": packages,
            "sha256": {"disk": sha256(target_disk),
                       "serial": sha256(target_serial), "pcap": sha256(target_pcap),
                       "reopen_serial": sha256(target_reopen_serial)}}
    with tempfile.NamedTemporaryFile("w", prefix=target_meta.name + ".",
                                     suffix=".tmp", dir=target_meta.parent,
                                     delete=False) as stream:
        tmp = Path(stream.name)
        json.dump(info, stream, sort_keys=True, indent=2)
        stream.write("\n")
    try:
        os.chmod(tmp, 0o644)
        os.replace(tmp, target_meta)
    finally:
        if tmp.exists():
            tmp.unlink()
    print(f"[M204 CHECKPOINT CAPTURED] {port} verified after TWO cold-public QEMU boots and clean ext4; "
          "cache replay remains a separate, weaker acceptance mode")


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    if len(sys.argv) == 13 and sys.argv[1] == "capture":
        _, _, port, root, disk, serial, pcap, reopen, dst_disk, dst_meta, dst_serial, dst_pcap, dst_reopen = sys.argv
        capture(port, Path(root), Path(disk), Path(serial), Path(pcap), Path(reopen),
                Path(dst_disk), Path(dst_meta), Path(dst_serial), Path(dst_pcap),
                Path(dst_reopen), repo)
    elif len(sys.argv) == 9 and sys.argv[1] == "verify":
        _, _, port, root, disk, meta, serial, pcap, reopen = sys.argv
        verify(port, Path(root), Path(disk), Path(meta), Path(serial), Path(pcap),
               Path(reopen), repo)
    else:
        raise SystemExit("usage: m204-alpine-checkpoint.py capture PORT ROOT DISK FIRST_SERIAL PCAP "
                         "REOPEN_SERIAL CACHED_DISK CACHED_JSON CACHED_FIRST CACHED_PCAP CACHED_REOPEN | "
                         "verify PORT ROOT CACHED_DISK CACHED_JSON CACHED_FIRST CACHED_PCAP CACHED_REOPEN")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, UnicodeDecodeError) as exc:
        raise SystemExit(str(exc)) from exc
