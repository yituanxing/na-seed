#!/usr/bin/env python3
"""Fetch original Debian 13 *dynamic glibc* printf + libc6, fail closed.

This is deliberately independent of the verified static Debian and musl
fixtures.  An isolated apt update authenticates Debian's signed InRelease,
then exact-version Packages records pin each downloaded .deb's SHA256.
No host package is installed, no apt global state is modified, and no guest
PASS is ever printed by this host-side preparation command.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.request import urlopen


BASE = "https://deb.debian.org/debian/"
KEYRING = Path("/usr/share/keyrings/debian-archive-keyring.gpg")
VERSIONS = Path("scripts/m245-debian-versions.tsv")
PROGRAM = "usr/bin/printf"
TRIPLET = "x86_64-linux-gnu"
LOADER = "ld-linux-x86-64.so.2"
INTERP = "/lib64/ld-linux-x86-64.so.2"
EXPECTED_OUTPUT = b"M245-DEBIAN-DYNAMIC-GLIBC-OK\n"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_versions(path: Path, arch: str) -> dict[str, str]:
    versions = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 3 or not all(fields):
            raise ValueError(f"invalid version record: {line!r}")
        isa, name, version = fields
        if isa == arch:
            if name in versions:
                raise ValueError(f"duplicate version record: {name}")
            versions[name] = version
    if set(versions) != {"coreutils", "libc6"}:
        raise ValueError(f"missing official Debian dynamic dependencies for {arch}")
    return versions


def parse_apt_show(output: str, *, package: str, version: str, arch: str) -> dict[str, str]:
    """Select one *exact* architecture/version record from trusted apt lists."""
    matches = []
    for block in re.split(r"\n\s*\n", output.strip()):
        fields: dict[str, str] = {}
        for line in block.splitlines():
            if not line or line[0].isspace() or ":" not in line:
                continue
            key, value = line.split(":", 1)
            if key in fields:
                raise ValueError(f"duplicate apt field {key}")
            fields[key] = value.strip()
        if (fields.get("Package"), fields.get("Version"), fields.get("Architecture")) == (package, version, arch):
            matches.append(fields)
    if len(matches) != 1:
        raise ValueError(f"wanted one signed-index entry for {package}:{arch}={version}; found {len(matches)}")
    result = matches[0]
    digest = result.get("SHA256", "")
    pool_path = result.get("Filename", "")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("missing/invalid Packages SHA256")
    if not re.fullmatch(r"pool/main/[A-Za-z0-9+_.\-]+/[A-Za-z0-9+_.\-/]+\.deb", pool_path):
        raise ValueError("unsafe or non-Debian pool path in Packages")
    if ".." in pool_path or "//" in pool_path or pool_path.startswith("/"):
        raise ValueError("Packages path traversal")
    if not result.get("Size", "").isdigit() or int(result["Size"]) <= 0:
        raise ValueError("missing package length in signed index")
    return result


def apt_options(root: Path, *, arch: str) -> list[str]:
    return [
        "-o", f"Dir::Etc::sourcelist={root / 'sources.list'}",
        "-o", "Dir::Etc::sourceparts=-",
        "-o", f"Dir::State::lists={root / 'lists'}",
        "-o", f"Dir::State::status={root / 'status'}",
        "-o", f"Dir::Cache::archives={root / 'archives'}",
        "-o", f"APT::Architecture={arch}",
        "-o", f"APT::Architectures={arch}",
        "-o", "APT::Get::AllowUnauthenticated=false",
        "-o", "Acquire::AllowInsecureRepositories=false",
        "-o", "Acquire::AllowDowngradeToInsecureRepositories=false",
        "-o", "Acquire::Check-Valid-Until=true",
    ]


def run(*args: str, capture: bool = False) -> str:
    result = subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout if capture else ""


def fetch_verified_deb(record: dict[str, str], target: Path) -> None:
    expected = record["SHA256"]
    size = int(record["Size"])
    if target.is_file() and sha256_file(target) == expected and target.stat().st_size == size:
        return
    if target.exists():
        raise ValueError(f"existing .deb at {target} is not the signed-index object")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="m245-deb.", dir=target.parent)
    try:
        digest = hashlib.sha256()
        length = 0
        with os.fdopen(fd, "wb") as out, urlopen(BASE + record["Filename"], timeout=60) as response:
            while chunk := response.read(1024 * 1024):
                length += len(chunk)
                if length > size:
                    raise ValueError("download larger than authenticated Packages size")
                digest.update(chunk)
                out.write(chunk)
        if length != size or digest.hexdigest() != expected:
            raise ValueError("download does not match authenticated Packages size/SHA256")
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def elf_interpreter(program: Path) -> str:
    output = run("readelf", "-l", str(program), capture=True)
    matches = re.findall(r"\[Requesting program interpreter: ([^\]]+)\]", output)
    if len(matches) != 1:
        raise ValueError(f"original program needs exactly one PT_INTERP: {program}")
    return matches[0]


def validate_sysroot(root: Path) -> dict[str, str]:
    binary = root / PROGRAM
    libc = root / "usr/lib" / TRIPLET / "libc.so.6"
    loader = root / "usr/lib" / TRIPLET / LOADER
    for target in [binary, libc, loader]:
        if not target.is_file() or target.is_symlink():
            raise ValueError(f"missing genuine Debian ELF file: {target}")
        header = run("readelf", "-h", str(target), capture=True)
        if "Advanced Micro Devices X86-64" not in header or "ELF64" not in header:
            raise ValueError(f"wrong ISA or ELF class: {target}")
    header = run("readelf", "-h", str(binary), capture=True)
    if not re.search(r"Type:\s+DYN ", header):
        raise ValueError("Debian printf is not dynamic PIE")
    interp = elf_interpreter(binary)
    if interp != INTERP:
        raise ValueError(f"glibc PT_INTERP unexpectedly changed: {interp}")
    dynamic = run("readelf", "-d", str(binary), capture=True)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    if needed != ["libc.so.6"]:
        raise ValueError(f"unreviewed original printf DT_NEEDED closure: {needed}")
    return {str(path.relative_to(root)): sha256_file(path) for path in [binary, libc, loader]}


def linux_control(root: Path) -> None:
    loader = root / "usr/lib" / TRIPLET / LOADER
    libdir = root / "usr/lib" / TRIPLET
    program = root / PROGRAM
    command = [str(loader), "--library-path", str(libdir), str(program), EXPECTED_OUTPUT.decode("ascii")[:-1] + "\\n"]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    if result.returncode != 0 or result.stdout != EXPECTED_OUTPUT:
        raise ValueError(f"Linux reference failed rc={result.returncode}; stdout={result.stdout!r}; stderr={result.stderr[:500]!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["amd64"], required=True,
                        help="initial M245 gate intentionally starts only with genuine amd64")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    if not KEYRING.is_file() or KEYRING.stat().st_size < 100:
        raise SystemExit(f"M245 FAIL: Debian archive trust anchor missing: {KEYRING}")
    if not shutil.which("apt-get") or not shutil.which("apt-cache"):
        raise SystemExit("M245 FAIL: signed APT index verification unavailable")

    out = Path(args.out).absolute()
    out.mkdir(parents=True, exist_ok=True)
    apt = out / "apt"
    for subdir in ["lists/partial", "archives/partial"]:
        (apt / subdir).mkdir(parents=True, exist_ok=True)
    (apt / "status").touch(exist_ok=True)
    (apt / "sources.list").write_text(
        f"deb [arch={args.arch} signed-by={KEYRING}] {BASE} trixie main\n", encoding="ascii"
    )
    options = apt_options(apt, arch=args.arch)
    print("[M245 HOST] Authenticating Debian trixie InRelease with apt / Debian archive keyring", flush=True)
    run("apt-get", *options, "update")
    signed_release = list((apt / "lists").glob("*_dists_trixie_InRelease"))
    if len(signed_release) != 1 or not signed_release[0].is_file():
        raise ValueError("authenticated trixie InRelease missing/ambiguous")

    records = {}
    for package, version in expected_versions(VERSIONS, args.arch).items():
        listing = run("apt-cache", *options, "show", package, capture=True)
        record = parse_apt_show(listing, package=package, version=version, arch=args.arch)
        records[package] = record
        target = out / "debs" / Path(record["Filename"]).name
        fetch_verified_deb(record, target)
        print(f"[M245 HOST] signed Packages -> SHA256 verified {package}:{args.arch}={version} {record['SHA256']}")

    root = out / "sysroot"
    metadata_path = out / "manifest.json"
    if root.exists():
        if not metadata_path.is_file():
            raise ValueError(f"existing sysroot has no verifiable manifest: {root}")
        old = json.loads(metadata_path.read_text(encoding="utf-8"))
        if old.get("debs") != {name: record["SHA256"] for name, record in records.items()}:
            raise ValueError("existing sysroot was built from different Debian packages")
    else:
        stage = Path(tempfile.mkdtemp(prefix="m245-extract.", dir=out))
        try:
            for record in records.values():
                deb = out / "debs" / Path(record["Filename"]).name
                run("dpkg-deb", "-x", str(deb), str(stage))
            validate_sysroot(stage)
            stage.rename(root)
        finally:
            # No unverified leftovers masquerade as a successfully staged sysroot.
            if stage.exists():
                shutil.rmtree(stage)

    elf_sha = validate_sysroot(root)
    linux_control(root)
    manifest = {
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "distribution": "Debian 13 trixie",
        "architecture": args.arch,
        "trusted_index": "apt verified Debian archive signed InRelease -> Packages SHA256",
        "inrelease_sha256": sha256_file(signed_release[0]),
        "packages": {name: {"version": record["Version"], "sha256": record["SHA256"],
                            "pool": record["Filename"]} for name, record in records.items()},
        "debs": {name: record["SHA256"] for name, record in records.items()},
        "program_interpreter": INTERP,
        "program_dt_needed": ["libc.so.6"],
        "elf_sha256": elf_sha,
        "linux_control_stdout": EXPECTED_OUTPUT.decode("ascii"),
    }
    metadata_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[M245 HOST PREPARED, NOT GUEST PASS] {root} original Debian glibc printf control verified")


if __name__ == "__main__":
    main()
