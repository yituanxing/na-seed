#!/usr/bin/env python3
"""Validate the host-only signed Alpine APK overlay catalog.

The catalog owns profile -> package/ELF/copy manifest identity.  Legacy
milestone wrappers stay as stable CI entry points; they must not silently
register a second, divergent definition of an existing profile.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "scripts/prebuilt-profile-catalog.tsv"
HEADER = "profile\tpackages\telfs\tcopies\trepositories"
PROFILE_RE = re.compile(r"m[0-9]+-[a-z0-9-]+")
PACKAGE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+_=-]*")
OVERLAY_CALL_RE = re.compile(
    r"stage-alpine-prebuilt-overlay\.sh\s+['\"]?\$(?:1|port)['\"]?\s+(m[0-9]+-[a-z0-9-]+)"
)


def fail(message: str) -> None:
    raise ValueError(message)


def within_repo(raw: str, expected_dir: str, label: str) -> Path:
    path = PurePosixPath(raw)
    if (not raw or path.is_absolute() or ".." in path.parts
            or path.as_posix() != raw or not raw.startswith(expected_dir + "/")):
        fail(f"{label}: noncanonical repository path: {raw!r}")
    full = ROOT / raw
    if not full.is_file() or not full.resolve().is_relative_to(ROOT):
        fail(f"{label}: missing or external regular file: {raw}")
    return full


def guest_path(raw: str, label: str, prefix: str = "") -> None:
    path = PurePosixPath(raw)
    if (not raw or path.is_absolute() or ".." in path.parts
            or path.as_posix() != raw or any(c.isspace() for c in raw)
            or (prefix and not raw.startswith(prefix))):
        fail(f"{label}: invalid guest path: {raw!r}")


def noncomment_lines(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def validate_manifests(profile: str, packages: Path, elfs: Path, copies: Path) -> None:
    package_lines = noncomment_lines(packages)
    elf_lines = noncomment_lines(elfs)
    copy_lines = noncomment_lines(copies)
    if not package_lines or not elf_lines or not copy_lines:
        fail(f"{profile}: package, ELF and copy manifests must all be nonempty")
    for line in package_lines:
        for package in line.split():
            if not PACKAGE_RE.fullmatch(package):
                fail(f"{profile}: invalid signed APK package name {package!r}")
    if len(set(elf_lines)) != len(elf_lines):
        fail(f"{profile}: duplicate ELF path")
    for program in elf_lines:
        guest_path(program, profile)
    destinations: set[str] = set()
    for line in copy_lines:
        fields = line.split("\t")
        if len(fields) != 2:
            fail(f"{profile}: copy mapping must contain exactly one tab: {line!r}")
        source, destination = fields
        guest_path(source, profile)
        guest_path(destination, profile, "tests/")
        if not (source.startswith("tests/") or source.startswith("build/")):
            fail(f"{profile}: copy source must be a test or generated artifact: {source}")
        if source.startswith("tests/"):
            within_repo(source, "tests", profile)
        if destination in destinations:
            fail(f"{profile}: duplicate guest copy destination: {destination}")
        destinations.add(destination)


def load_catalog(path: Path) -> dict[str, tuple[str, str, str, str]]:
    lines = path.read_text().splitlines()
    if not lines or lines[0] != HEADER:
        fail(f"{path}: expected exact TSV header {HEADER!r}")
    result: dict[str, tuple[str, str, str, str]] = {}
    for lineno, line in enumerate(lines[1:], start=2):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 5:
            fail(f"{path}:{lineno}: expected five tab-separated fields")
        profile, packages_raw, elfs_raw, copies_raw, repositories = fields
        if not PROFILE_RE.fullmatch(profile) or profile in result:
            fail(f"{path}:{lineno}: invalid or duplicate profile {profile!r}")
        if repositories not in {"main", "main+community"}:
            fail(f"{path}:{lineno}: unknown signed Alpine repositories {repositories!r}")
        packages = within_repo(packages_raw, "rootfs/alpine", profile)
        elfs = within_repo(elfs_raw, "scripts", profile)
        copies = within_repo(copies_raw, "scripts", profile)
        validate_manifests(profile, packages, elfs, copies)
        result[profile] = (packages_raw, elfs_raw, copies_raw, repositories)
    if not result:
        fail(f"{path}: no host APK profiles")
    return result


def validate_wrappers(profiles: set[str]) -> int:
    wrappers: dict[str, str] = {}
    for path in sorted((ROOT / "scripts").glob("stage-m*-prebuilt.sh")):
        source = path.read_text()
        matches = OVERLAY_CALL_RE.findall(source)
        if len(matches) != 1:
            fail(f"{path.relative_to(ROOT)}: expected one canonical overlay call")
        profile = matches[0]
        if profile in wrappers:
            fail(f"{profile}: wrappers {wrappers[profile]} and {path.name} conflict")
        milestone = re.fullmatch(r"stage-(m[0-9]+)-prebuilt\.sh", path.name)
        if not milestone or not profile.startswith(milestone.group(1) + "-"):
            fail(f"{path.name}: profile {profile} does not match milestone")
        wrappers[profile] = path.name
    if profiles != wrappers.keys():
        fail(f"catalog/wrapper mismatch: missing={sorted(wrappers.keys() - profiles)}, "
             f"orphan={sorted(profiles - wrappers.keys())}")
    return len(wrappers)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=CATALOG)
    args = parser.parse_args()
    try:
        profiles = load_catalog(args.catalog)
        count = validate_wrappers(set(profiles))
    except (OSError, ValueError) as exc:
        print(f"[APK CATALOG FAIL] {exc}", file=sys.stderr)
        return 1
    print(f"[APK CATALOG PASS] {count} signed original Alpine overlays, "
          "canonical manifests and stable wrappers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
