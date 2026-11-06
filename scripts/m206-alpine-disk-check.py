#!/usr/bin/env python3
"""Independent M206 physical-disk oracle for 20 new original Alpine ELFs.

The normal M197 oracle remains responsible for public HTTPS, its four
original APKs, SQLite/Git data, MBR, fsck and clean second-boot persistence.
This additional oracle reads the *same physical ext4 bytes* independently
with debugfs to prove that every M206 APK and native ELF survived reboot.
Neither a guest UART PASS string nor a host staging file can satisfy it.
"""

from __future__ import annotations

import csv
import hashlib
import importlib.util
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
CANDIDATES = ROOT / "scripts/m206-candidate-elfs.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m206-candidates.txt"
HEADER = ["package", "elf_path", "family", "probe"]
PACKAGE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+_-]*\Z")
TAG = re.compile(r"[a-z][a-z0-9-]*\Z")


def load(name: str, alias: str):
    source = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, source)
    if spec is None or spec.loader is None:
        raise RuntimeError("M206 missing existing host oracle " + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def manifest(path: Path = CANDIDATES) -> tuple[list[str], list[str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        records = csv.reader(stream, delimiter="\t")
        if next(records, None) != HEADER:
            raise ValueError("[M206 HOST FAIL] incorrect exact candidate header")
        package_names: list[str] = []
        binary_paths: list[str] = []
        for line in records:
            if not line or line[0].startswith("#"):
                continue
            if len(line) != 4:
                raise ValueError("[M206 HOST FAIL] invalid four-column candidate")
            package, elf, family, probe = line
            posix = PurePosixPath(elf)
            if (not PACKAGE.fullmatch(package) or not TAG.fullmatch(family) or
                    not TAG.fullmatch(probe) or
                    not elf.startswith(("bin/", "sbin/", "usr/bin/", "usr/sbin/")) or
                    ".." in posix.parts or posix.is_absolute() or
                    posix.as_posix() != elf or any(c.isspace() for c in elf)):
                raise ValueError("[M206 HOST FAIL] malformed candidate path")
            package_names.append(package)
            binary_paths.append("/" + elf)
    if len(binary_paths) != 20 or len(set(binary_paths)) != 20:
        raise ValueError("[M206 HOST FAIL] expected 20 distinct original ELF paths")
    return sorted(set(package_names)), binary_paths


def packages_in_world(path: Path = PACKAGES) -> list[str]:
    values = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
              if line.strip() and not line.lstrip().startswith("#")]
    if (len(values) != len(set(values)) or not values or
            any(not PACKAGE.fullmatch(package) for package in values)):
        raise ValueError("[M206 HOST FAIL] duplicate/missing package requests")
    return sorted(values)


def verify(port: str, signed_root: Path, disk: Path, *,
           candidates: Path = CANDIDATES, packages: Path = PACKAGES) -> None:
    package_names, binary_paths = manifest(candidates)
    package_requests = packages_in_world(packages)
    if package_names != package_requests:
        raise ValueError("[M206 HOST FAIL] candidate owners differ from APK requests")

    m197 = load("m197-alpine-disk-check.py", "m206_m197")
    # M197 performs the actual physical GPT/ext4+fsck, public HTTPS and
    # original Bash/Lua/SQLite/Git verifications before M206's extra checks.
    m197.verify(port, signed_root, disk)
    m191 = m197.load("m195-alpine-disk-check.py", "m206_m195").load_m191()
    if port not in m191.ARCH_MACHINE:
        raise ValueError("[M206 HOST FAIL] unsupported guest ISA")
    signed_root = signed_root.resolve(strict=True)
    disk = disk.resolve(strict=True)
    for path in binary_paths:
        # The M206 ELFs must come from the guest's APK transaction, not the
        # host's preconfigured root image or a BusyBox applet masquerading
        # as an independently installed signed Alpine program.
        staged = m191.guest_path(signed_root, path)
        if staged.exists() or staged.is_symlink():
            raise ValueError("[M206 HOST FAIL] candidate preinstalled in rootfs: " + path)

    with tempfile.TemporaryDirectory(prefix="m206-physical-disk-") as work:
        partition = Path(work) / "root.ext4"
        m191.extract_partition(disk, partition)
        read = lambda name: m191.debugfs_cat(partition, name)
        installed = "\n" + read("/lib/apk/db/installed").decode("utf-8")
        world = set(read("/etc/apk/world").decode("utf-8").splitlines())
        for name in package_names:
            if name not in world:
                raise ValueError("[M206 HOST FAIL] APK world missing " + name)
            if f"\nP:{name}\n" not in installed:
                raise ValueError("[M206 HOST FAIL] installed database missing " + name)

        fingerprints: set[str] = set()
        for path in binary_paths:
            binary = read(path)
            if len(binary) < 20 or binary[:6] != b"\x7fELF\x02\x01":
                raise ValueError("[M206 HOST FAIL] native ELF missing: " + path)
            machine = struct.unpack_from("<H", binary, 18)[0]
            if machine != m191.ARCH_MACHINE[port]:
                raise ValueError("[M206 HOST FAIL] wrong ELF ISA: " + path)
            fingerprint = hashlib.sha256(binary).hexdigest()
            if fingerprint in fingerprints:
                raise ValueError("[M206 HOST FAIL] repeated ELF SHA256: " + path)
            fingerprints.add(fingerprint)
    print(f"[M206 ORIGINAL APK DISK PASS] {port} {len(package_names)} signed-package "
          "records in APK world+installed DB, 20 original ISA-matched unique-SHA ELFs "
          "persisted after native second boot, M197 fsck/public-software proof intact")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m206-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
