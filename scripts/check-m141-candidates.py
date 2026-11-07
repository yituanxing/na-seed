#!/usr/bin/env python3
"""Preflight an *unaccepted* Alpine ELF discovery pool.

This is intentionally a host-only, zero-QEMU check. Passing it never counts
an ELF as accepted: signed APK origin, actual ELF machine/SHA, guest execution
and semantic correctness are still checked by the later native-QEMU batch.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parent.parent
HEADER = "package\telf_path\tfamily\tprobe"
PACKAGE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+_-]*")
TAG = re.compile(r"[a-z][a-z0-9-]*")


def entries(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def main(profile: str = "m141", minimum: int = 50) -> None:
    if profile not in ("m141", "m205", "m206", "m213", "m215", "m218", "m219", "m220"):
        raise ValueError(f"unrecognized candidate profile: {profile}")
    pool = ROOT / f"scripts/{profile}-candidate-elfs.tsv"
    packages_path = ROOT / f"rootfs/alpine/packages-{profile}-candidates.txt"
    packages = set(entries(packages_path))
    if not packages or any(not PACKAGE.fullmatch(item) for item in packages):
        raise ValueError("invalid candidate package manifest")

    known: set[str] = set()
    for manifest in sorted((ROOT / "scripts").glob("m[0-9]*-original-elfs.txt")):
        if manifest.name == f"{profile}-original-elfs.txt":
            continue
        if profile in ("m213", "m215", "m218", "m219", "m220") and manifest.name == "m141-original-elfs.txt":
            # M141 is an explicitly UNACCEPTED 68-ELF discovery pool.
            # M213 deliberately promotes a subset through fresh real QEMU
            # semantics; provenance there must not count as prior success.
            continue
        known.update(entries(manifest))
    if profile in ("m206", "m213", "m215", "m218", "m219", "m220"):
        # M205 is a real new-candidate batch but has no historical accepted
        # m*-original-elfs.txt manifest yet; excluding only the latter would
        # silently count the same original command again.
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m205-candidate-elfs.tsv")[1:])
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m204-alpine-cohort.tsv")[1:]
                     if len(row.split("\t")) >= 2)
    if profile in ("m213", "m215", "m218", "m219", "m220"):
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m206-candidate-elfs.tsv")[1:])
    if profile in ("m218", "m219", "m220"):
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m215-candidate-elfs.tsv")[1:])
    if profile in ("m219", "m220"):
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m218-candidate-elfs.tsv")[1:])
    if profile == "m220":
        known.update(row.split("\t")[1] for row in
                     entries(ROOT / "scripts/m219-candidate-elfs.tsv")[1:])

    lines = pool.read_text().splitlines()
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

    if len(seen) < minimum:
        raise ValueError(f"pool too small: {len(seen)} candidates (need at least {minimum})")
    minimum_families = (1 if profile in ("m218", "m219", "m220") else
                        3 if profile == "m206" else
                        4 if profile == "m215" else 5)
    if len(families) < minimum_families:
        raise ValueError(f"pool lacks diversity: {len(families)} families "
                         f"(need at least {minimum_families})")
    label=profile.upper()
    print(f"[{label} CANDIDATE PREFLIGHT PASS] {len(seen)} unaccepted paths, "
          f"{len(packages)} signed-APK package requests, {len(families)} families")
    print(f"[{label} NOT ACCEPTED] native ELF/SHA and QEMU semantic checks pending")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("m141", "m205", "m206", "m213", "m215", "m218", "m219", "m220"), default="m141")
    parser.add_argument("--minimum", type=int)
    args = parser.parse_args()
    main(args.profile, args.minimum if args.minimum is not None else
         (50 if args.profile == "m141" else 5 if args.profile == "m215" else
          1 if args.profile in ("m218", "m219", "m220") else 20))
