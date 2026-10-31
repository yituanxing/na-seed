#!/usr/bin/env python3
"""Preflight the *unaccepted* M141 Alpine ELF discovery pool.

This is intentionally a host-only, zero-QEMU check. Passing it never counts
an ELF as accepted: signed APK origin, actual ELF machine/SHA, guest execution
and semantic correctness are still checked by the later native-QEMU batch.
"""

from __future__ import annotations

import re
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parent.parent
POOL = ROOT / "scripts/m141-candidate-elfs.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m141-candidates.txt"
HEADER = "package\telf_path\tfamily\tprobe"
PACKAGE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+_-]*")
TAG = re.compile(r"[a-z][a-z0-9-]*")


def entries(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def main() -> None:
    packages = set(entries(PACKAGES))
    if not packages or any(not PACKAGE.fullmatch(item) for item in packages):
        raise ValueError("invalid candidate package manifest")

    known: set[str] = set()
    for manifest in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
        if manifest.name == "m141-original-elfs.txt":
            continue
        known.update(entries(manifest))

    lines = POOL.read_text().splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError(f"expected TSV header {HEADER!r}")

    seen: set[str] = set()
    families: set[str] = set()
    for number, line in enumerate(lines[1:], 2):
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) != 4:
            raise ValueError(f"line {number}: expected four tab-separated fields")
        package, raw, family, probe = cols
        path = PurePosixPath(raw)
        if package not in packages or not TAG.fullmatch(family) or not TAG.fullmatch(probe):
            raise ValueError(f"line {number}: invalid package/family/probe")
        if (path.is_absolute() or ".." in path.parts or path.as_posix() != raw
                or not raw.startswith(("usr/bin/", "usr/sbin/", "bin/", "sbin/"))
                or any(c.isspace() for c in raw)):
            raise ValueError(f"line {number}: invalid target path {raw!r}")
        if raw in known:
            raise ValueError(f"line {number}: already covered by earlier batch: {raw}")
        if raw in seen:
            raise ValueError(f"line {number}: duplicate candidate path: {raw}")
        seen.add(raw)
        families.add(family)

    if len(seen) < 50:
        raise ValueError(f"pool too small: {len(seen)} candidates (need at least 50)")
    if len(families) < 5:
        raise ValueError(f"pool lacks diversity: {len(families)} families (need at least 5)")
    print(f"[M141 CANDIDATE PREFLIGHT PASS] {len(seen)} unaccepted paths, "
          f"{len(packages)} signed-APK package requests, {len(families)} families")
    print("[M141 NOT ACCEPTED] native ELF/SHA and QEMU semantic checks pending")


if __name__ == "__main__":
    main()
