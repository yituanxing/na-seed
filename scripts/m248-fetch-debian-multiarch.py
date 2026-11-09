#!/usr/bin/env python3
"""Authenticate and stage original Debian 13 glibc dash/coreutils for 3 ISAs.

The native guest run is deliberately separate. A successful fetch is **never**
reported as a NaSeed QEMU PASS. M245/M247 x86 baselines are left unchanged.
All downloaded .debs must match the SHA256 *and* Size of an architecture- and
version-exact Packages record from APT-authenticated trixie InRelease.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


FETCHER = Path(__file__).with_name("m245-fetch-debian-dynamic.py")
spec = importlib.util.spec_from_file_location("m245_signed_debian_helpers", FETCHER)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot import established signed APT helpers: {FETCHER}")
m245 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m245)

PINS = Path(__file__).with_name("m248-debian-multiarch-versions.tsv")
PACKAGES = frozenset({"dash", "coreutils", "libc6"})
PROGRAMS = ("dash", "true", "cat", "wc", "printf")
PORTS = {
    "amd64": ("x86_64-linux-gnu", "Advanced Micro Devices X86-64",
              "/lib64/ld-linux-x86-64.so.2"),
    "arm64": ("aarch64-linux-gnu", "AArch64", "/lib/ld-linux-aarch64.so.1"),
    "riscv64": ("riscv64-linux-gnu", "RISC-V",
                "/lib/ld-linux-riscv64-lp64d.so.1"),
}


def pinned_versions(path: Path, arch: str) -> dict[str, str]:
    """Reject absent, duplicate, incomplete, or malformed exact version pins."""
    if arch not in PORTS:
        raise ValueError(f"unsupported original Debian architecture: {arch}")
    versions: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 3 or not all(fields):
            raise ValueError(f"invalid multiarch version pin line: {line!r}")
        isa, name, version = fields
        if isa not in PORTS or name not in PACKAGES or not re.fullmatch(
                r"[0-9][A-Za-z0-9.+:~\-]*", version):
            raise ValueError(f"invalid architecture/package/version pin: {line!r}")
        if isa == arch:
            if name in versions:
                raise ValueError(f"duplicate {arch} version pin: {name}")
            versions[name] = version
    if set(versions) != PACKAGES:
        raise ValueError(f"missing exact Debian pins for {arch}: {sorted(PACKAGES - versions.keys())}")
    return versions


def authenticated_inrelease(apt: Path) -> Path:
    """APT must have verified precisely one signed trixie index."""
    matches = list((apt / "lists").glob("*_dists_trixie_InRelease"))
    if len(matches) != 1 or not matches[0].is_file() or matches[0].is_symlink():
        raise ValueError("missing/ambiguous signed trixie InRelease after apt update")
    if not matches[0].read_bytes().startswith(b"-----BEGIN PGP SIGNED MESSAGE-----"):
        raise ValueError("APT trixie InRelease is not a clearsigned index")
    return matches[0]


def original_regular_file(root: Path, relative: Path) -> Path:
    """Allow Debian's own merged-usr symlinks, but never an escape from sysroot."""
    path = root / relative
    if not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"missing/unsafe original Debian ELF: {path}")
    return path


def inspect_elf(path: Path, machine: str, *, executable: bool) -> dict[str, object]:
    header = m245.run("readelf", "-h", str(path), capture=True)
    if "ELF64" not in header or machine not in header:
        raise ValueError(f"wrong original Debian ELF machine/class: {path}")
    if not executable:
        return {}
    if not re.search(r"Type:\s+DYN\s", header):
        raise ValueError(f"original Debian user program is not dynamic PIE: {path}")
    interpreter = m245.elf_interpreter(path)
    dynamic = m245.run("readelf", "-d", str(path), capture=True)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    if needed != ["libc.so.6"]:
        raise ValueError(f"unreviewed Debian DT_NEEDED dependencies: {path}: {needed}")
    return {"interpreter": interpreter, "dt_needed": needed}


def validate_sysroot(root: Path, arch: str) -> dict[str, object]:
    triplet, machine, interp = PORTS[arch]
    selected: dict[str, Path] = {
        name: original_regular_file(root, Path("usr/bin") / name)
        for name in PROGRAMS
    }
    selected["libc.so.6"] = original_regular_file(
        root, Path("usr/lib") / triplet / "libc.so.6")
    selected["loader"] = original_regular_file(
        root, Path("usr/lib") / triplet / Path(interp).name)
    binary_props = {}
    for name, path in selected.items():
        props = inspect_elf(path, machine, executable=name in PROGRAMS)
        if name in PROGRAMS:
            if props["interpreter"] != interp:
                raise ValueError(f"wrong {arch} original Debian PT_INTERP for {name}: {props['interpreter']}")
            binary_props[name] = props
    return {
        "program_interpreter": interp,
        "program_dt_needed": binary_props,
        "elf_sha256": {str(path.relative_to(root)): m245.sha256_file(path)
                       for path in selected.values()},
    }


def fetch(arch: str, out: Path, versions_file: Path = PINS) -> dict[str, object]:
    """Use only signed per-arch trixie APT indexes and SHA-bound pool artifacts."""
    if arch not in PORTS:
        raise ValueError(f"unsupported original Debian architecture: {arch}")
    keyring = m245.KEYRING
    if not keyring.is_file() or keyring.stat().st_size < 100:
        raise ValueError(f"Debian archive trust anchor missing: {keyring}")
    for tool in ("apt-get", "apt-cache", "dpkg-deb", "readelf"):
        if not shutil.which(tool):
            raise ValueError(f"signed Debian acquisition requires {tool}")
    versions = pinned_versions(versions_file, arch)
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    apt = out / "apt"
    for relative in ("lists/partial", "archives/partial"):
        (apt / relative).mkdir(parents=True, exist_ok=True)
    (apt / "status").touch(exist_ok=True)
    (apt / "sources.list").write_text(
        f"deb [arch={arch} signed-by={keyring}] {m245.BASE} trixie main\n",
        encoding="ascii")
    options = m245.apt_options(apt, arch=arch)
    print(f"[M248 SIGNED HOST] apt authentication trixie/{arch}", flush=True)
    m245.run("apt-get", *options, "update")
    index = authenticated_inrelease(apt)

    records: dict[str, dict[str, str]] = {}
    for package, version in versions.items():
        listing = m245.run("apt-cache", *options, "show", package, capture=True)
        record = m245.parse_apt_show(
            listing, package=package, version=version, arch=arch)
        records[package] = record
        m245.fetch_verified_deb(record, out / "debs" / Path(record["Filename"]).name)
        print(f"[M248 SIGNED HOST] {package}:{arch}={version} SHA256 {record['SHA256']}", flush=True)

    root = out / "sysroot"
    manifest_path = out / "manifest.json"
    package_sha = {name: record["SHA256"] for name, record in records.items()}
    previous = None
    if root.exists():
        if root.is_symlink() or not manifest_path.is_file():
            raise ValueError("unverified preexisting M248 sysroot")
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        if previous.get("architecture") != arch or previous.get("debs") != package_sha:
            raise ValueError("preexisting M248 sysroot provenance mismatch")
    else:
        stage = Path(tempfile.mkdtemp(prefix="m248-stage-", dir=out))
        try:
            for record in records.values():
                m245.run("dpkg-deb", "-x", str(out / "debs" / Path(record["Filename"]).name), str(stage))
            validate_sysroot(stage, arch)
            stage.rename(root)
        finally:
            if stage.exists():
                shutil.rmtree(stage)

    info = validate_sysroot(root, arch)
    if previous is not None and previous.get("elf_sha256") != info["elf_sha256"]:
        raise ValueError("preexisting M248 sysroot ELF bytes changed since signed package staging")
    metadata: dict[str, object] = {
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "distribution": "Debian 13 trixie",
        "architecture": arch,
        "trusted_index": "Debian-keyring-authenticated signed InRelease -> Packages SHA256/Size",
        "inrelease_sha256": m245.sha256_file(index),
        "debs": package_sha,
        "packages": {name: {"version": record["Version"],
                            "sha256": record["SHA256"],
                            "size": int(record["Size"]),
                            "pool": record["Filename"]}
                     for name, record in records.items()},
        **info,
    }
    manifest_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[M248 HOST PREPARED, NOT GUEST PASS] {arch} {root}", flush=True)
    return metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True, choices=list(PORTS))
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    fetch(args.arch, args.out)


if __name__ == "__main__":
    main()
