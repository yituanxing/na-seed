#!/usr/bin/env python3
"""M251: supplement M248's signed Debian 13/glibc fixture with 7 coreutils.

The original M248 manifest and M249/M250 disk contracts are never rewritten.
Additional ELF dependency libraries come only from exact-version Packages
records in the *same* APT-authenticated InRelease index as M248. This host
preparation does not establish that any guest executable ran in NaSeed.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "m248_signed_debian_for_m251", HERE / "m248-fetch-debian-multiarch.py")
assert SPEC and SPEC.loader
m248 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m248)
m245 = m248.m245

PROGRAMS = ("ls", "head", "sort", "mkdir", "rm", "env", "sleep")
# This is a deliberately bounded, software-driven closure, not a generic
# resolver that silently accepts new libraries from an unreviewed package.
LIBRARY_PACKAGES = {
    "libselinux.so.1": "libselinux1",
    "libcap.so.2": "libcap2",
    "libpcre2-8.so.0": "libpcre2-8-0",
    "libacl.so.1": "libacl1",
    "libattr.so.1": "libattr1",
    "libgmp.so.10": "libgmp10",
}
SCHEMA = "m251-signed-debian-trixie-extra-coreutils-v1"


def require(ok: bool, reason: str) -> None:
    if not ok:
        raise ValueError("[M251 DEBIAN PROVENANCE FAIL] " + reason)


def read_json(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(data, dict), f"invalid JSON object: {path}")
    return data


def source_context(port: str, manifest_path: Path) -> tuple[Path, Path, dict]:
    require(port in m248.PORTS, "unsupported Debian architecture")
    manifest_path = manifest_path.resolve()
    source_dir = manifest_path.parent
    source = source_dir / "sysroot"
    apt = source_dir / "apt"
    manifest = read_json(manifest_path)
    require(manifest.get("architecture") == port and
            manifest.get("distribution") == "Debian 13 trixie" and
            manifest.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "M248 signed manifest identity mismatch")
    require(source.is_dir() and not source.is_symlink(), "M248 signed sysroot absent")
    info = m248.validate_sysroot(source, port)
    require(info["elf_sha256"] == manifest.get("elf_sha256") and
            info["program_interpreter"] == manifest.get("program_interpreter") and
            info["program_dt_needed"] == manifest.get("program_dt_needed"),
            "M248 signed sysroot ELF contents/ABI diverged")
    release = m248.authenticated_inrelease(apt)
    require(m245.sha256_file(release) == manifest.get("inrelease_sha256"),
            "M248 signed InRelease index changed")
    require(isinstance(manifest.get("packages"), dict) and
            manifest["packages"].get("coreutils", {}).get("sha256") ==
            manifest.get("debs", {}).get("coreutils"),
            "M248 coreutils signed .deb source missing")
    return source, apt, manifest


def verified_coreutils_extract(manifest_path: Path, manifest: dict, stage: Path) -> Path:
    package = manifest["packages"]["coreutils"]
    source_dir = manifest_path.resolve().parent
    deb = source_dir / "debs" / Path(package["pool"]).name
    require(deb.is_file() and not deb.is_symlink(), "original coreutils .deb absent")
    require(m245.sha256_file(deb) == package["sha256"] and
            deb.stat().st_size == package["size"],
            "original coreutils .deb does not match signed SHA256/Size")
    extracted = stage / "coreutils"
    m245.run("dpkg-deb", "-x", str(deb), str(extracted))
    return extracted


def elf_properties(path: Path, port: str, *, executable: bool) -> dict:
    _, machine, interpreter = m248.PORTS[port]
    header = m245.run("readelf", "-h", str(path), capture=True)
    require("ELF64" in header and machine in header and
            re.search(r"Type:\s+DYN\s", header) is not None,
            f"wrong native PIE/shared ELF class/ISA: {path}")
    dynamic = m245.run("readelf", "-d", str(path), capture=True)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    require(len(needed) == len(set(needed)), f"duplicate DT_NEEDED: {path}")
    allowed = {"libc.so.6", Path(interpreter).name, *LIBRARY_PACKAGES}
    require(all(soname in allowed for soname in needed),
            f"unreviewed DT_NEEDED: {path}: {needed}")
    require("libc.so.6" in needed, f"glibc DT_NEEDED missing: {path}")
    props = {"dt_needed": needed}
    if executable:
        require(m245.elf_interpreter(path) == interpreter,
                f"wrong glibc PT_INTERP: {path}")
        props["interpreter"] = interpreter
    return props


def copy_signed_bytes(source: Path, destination: Path) -> str:
    require(source.is_file(), f"signed source ELF missing: {source}")
    digest = m245.sha256_file(source)
    if destination.exists() or destination.is_symlink():
        require(destination.is_file() and not destination.is_symlink() and
                m245.sha256_file(destination) == digest,
                f"would overwrite incompatible guest file: {destination}")
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    return digest


def signed_dependency_record(apt: Path, port: str, package: str) -> dict[str, str]:
    opts = m245.apt_options(apt, arch=port)
    policy = m245.run("apt-cache", *opts, "policy", package, capture=True)
    match = re.search(r"^\s*Candidate:\s*(\S+)\s*$", policy, re.MULTILINE)
    require(match is not None and match.group(1) != "(none)",
            f"no authenticated trixie candidate for {package}:{port}")
    version = match.group(1)
    require(re.fullmatch(r"[0-9][A-Za-z0-9.+:~\-]*", version) is not None,
            f"invalid package version: {package}:{port}={version}")
    listing = m245.run("apt-cache", *opts, "show", package, capture=True)
    try:
        return m245.parse_apt_show(listing, package=package, version=version, arch=port)
    except ValueError as exc:
        # M245's older filename grammar was intentionally scoped to simple
        # coreutils/libc6 .debs. Official Debian dependency archives may use
        # '~' in a version or percent-encode an epoch colon in a pool filename.
        # Keep its exact package/version/arch/SHA/size policy, but explicitly
        # review the expanded Debian pool grammar here, without changing M245.
        if str(exc) != "unsafe or non-Debian pool path in Packages":
            raise
    matches = []
    for block in re.split(r"\n\s*\n", listing.strip()):
        record = {}
        for line in block.splitlines():
            if not line or line[0].isspace() or ":" not in line:
                continue
            key, value = line.split(":", 1)
            require(key not in record, f"duplicate signed Packages field: {key}")
            record[key] = value.strip()
        if (record.get("Package"), record.get("Version"),
                record.get("Architecture")) == (package, version, port):
            matches.append(record)
    require(len(matches) == 1,
            f"wanted exactly one signed dependency package record for {package}:{port}={version}")
    record = matches[0]
    pool = record.get("Filename", "")
    # Allow only fixed deb.debian.org pool/main path segments. Encoded colons
    # (%3A) are the sole percent escape; no encoded /, .., query or fragment.
    safe_piece = r"(?:[A-Za-z0-9+_.~:\-]|%3[aA])+"
    require(re.fullmatch(rf"pool/main/(?:{safe_piece}/){{1,4}}{safe_piece}\.deb", pool)
            is not None and ".." not in pool and "//" not in pool and
            pool.rsplit("/", 1)[-1].startswith(package + "_"),
            f"unsafe authenticated dependency pool path for {package}: {pool!r}")
    require(re.fullmatch(r"[0-9a-f]{64}", record.get("SHA256", "")) is not None,
            f"missing signed SHA256 for dependency {package}")
    require(record.get("Size", "").isdigit() and int(record["Size"]) > 0,
            f"missing signed Size for dependency {package}")
    return record


def locate_original_library(extract: Path, triplet: str, soname: str) -> Path:
    path = extract / "usr/lib" / triplet / soname
    if not path.is_file():
        # Debian's merged-/usr packages may still carry a /lib path.
        path = extract / "lib" / triplet / soname
    require(path.is_file() and path.resolve().is_relative_to(extract.resolve()),
            f"authenticated .deb missing {triplet}/{soname}")
    return path


def prepare(port: str, root: Path, manifest_path: Path, extra_path: Path) -> dict:
    root = root.resolve()
    require(root.is_dir() and not root.is_symlink(), "guest ext4 root tree absent")
    source, apt, base = source_context(port, manifest_path)
    triplet, _, _ = m248.PORTS[port]
    metadata = {
        "schema": SCHEMA,
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "distribution": "Debian 13 trixie",
        "architecture": port,
        "base_manifest_sha256": m245.sha256_file(manifest_path),
        "inrelease_sha256": base["inrelease_sha256"],
        "coreutils_package": base["packages"]["coreutils"],
        "programs": {}, "libraries": {}, "dependency_packages": {},
        "elf_sha256": {},
    }
    with tempfile.TemporaryDirectory(prefix="m251-signed-extract-") as scratch:
        stage = Path(scratch)
        unpacked = verified_coreutils_extract(manifest_path, base, stage)
        pending = []
        for name in PROGRAMS:
            relative = f"usr/bin/{name}"
            original = m248.original_regular_file(source, Path(relative))
            authenticated = m248.original_regular_file(unpacked, Path(relative))
            require(m245.sha256_file(original) == m245.sha256_file(authenticated),
                    f"M248 sysroot {relative} differs from authenticated coreutils .deb")
            props = elf_properties(authenticated, port, executable=True)
            digest = copy_signed_bytes(authenticated, root / relative)
            metadata["programs"][relative] = {"sha256": digest, **props}
            metadata["elf_sha256"][relative] = digest
            pending.extend(props["dt_needed"])

        while pending:
            soname = pending.pop(0)
            if soname == "libc.so.6" or soname == Path(m248.PORTS[port][2]).name:
                continue
            require(soname in LIBRARY_PACKAGES,
                    f"unreviewed dependency SONAME: {soname}")
            relative = f"usr/lib/{triplet}/{soname}"
            if relative in metadata["libraries"]:
                continue
            package = LIBRARY_PACKAGES[soname]
            if package not in metadata["dependency_packages"]:
                record = signed_dependency_record(apt, port, package)
                deb_target = manifest_path.resolve().parent / "debs" / Path(record["Filename"]).name
                m245.fetch_verified_deb(record, deb_target)
                package_root = stage / package
                m245.run("dpkg-deb", "-x", str(deb_target), str(package_root))
                metadata["dependency_packages"][package] = {
                    "version": record["Version"],
                    "sha256": record["SHA256"],
                    "size": int(record["Size"]),
                    "pool": record["Filename"],
                }
            signed_lib = locate_original_library(stage / package, triplet, soname)
            props = elf_properties(signed_lib, port, executable=False)
            digest = copy_signed_bytes(signed_lib, root / relative)
            metadata["libraries"][relative] = {
                "source_package": package, "sha256": digest, **props}
            metadata["elf_sha256"][relative] = digest
            pending.extend(props["dt_needed"])

    # All required SONAMEs must be physically present; a successful readelf
    # inspection alone cannot prove that the Debian dynamic linker can load.
    verify_root(port, root, manifest_path, metadata)
    extra_path.parent.mkdir(parents=True, exist_ok=True)
    rendered = json.dumps(metadata, sort_keys=True, indent=2) + "\n"
    if extra_path.exists():
        require(extra_path.read_text(encoding="utf-8") == rendered,
                "existing M251 extra provenance changed (no silent overwrite)")
    else:
        extra_path.write_text(rendered, encoding="utf-8")
    print(f"[M251 SIGNED HOST PREPARED, NOT NATIVE GUEST PASS] {port} "
          f"{len(PROGRAMS)} original coreutils; "
          f"{len(metadata['libraries'])} authenticated dependency DSOs; "
          f"SHA manifest {extra_path}", flush=True)
    return metadata


def verify_root(port: str, root: Path, manifest_path: Path, extra: dict | Path) -> dict:
    root = root.resolve()
    info = read_json(extra) if isinstance(extra, Path) else extra
    source, _, base = source_context(port, manifest_path)
    require(info.get("schema") == SCHEMA and
            info.get("architecture") == port and
            info.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" and
            info.get("inrelease_sha256") == base["inrelease_sha256"] and
            info.get("base_manifest_sha256") == m245.sha256_file(manifest_path) and
            info.get("coreutils_package") == base["packages"]["coreutils"],
            "supplemental provenance differs from M248 signed source")
    programs = info.get("programs")
    libraries = info.get("libraries")
    file_digests = info.get("elf_sha256")
    require(isinstance(programs, dict) and isinstance(libraries, dict) and
            isinstance(file_digests, dict) and
            set(programs) == {f"usr/bin/{name}" for name in PROGRAMS} and
            set(file_digests) == set(programs) | set(libraries),
            "missing or unexpected extra coreutils and/or DSO ELF set")
    closure = set()
    for relative, data in {**programs, **libraries}.items():
        require(relative in file_digests and isinstance(data, dict) and
                data.get("sha256") == file_digests[relative],
                f"invalid supplemental digest metadata: {relative}")
        path = root / relative
        require(path.is_file() and not path.is_symlink() and
                path.resolve().is_relative_to(root) and
                m245.sha256_file(path) == data["sha256"],
                f"guest root original ELF bytes changed: {relative}")
        props = elf_properties(path, port, executable=relative in programs)
        require(all(data.get(key) == value for key, value in props.items()),
                f"guest original ELF ABI changed: {relative}")
        closure.update(props["dt_needed"])
    triplet = m248.PORTS[port][0]
    expected_libs = {f"usr/lib/{triplet}/{item}" for item in closure if item in LIBRARY_PACKAGES}
    require(set(libraries) == expected_libs,
            f"incomplete or unexpected authenticated DSO closure: {sorted(expected_libs ^ set(libraries))}")
    require(set(info.get("dependency_packages", {})) ==
            {entry["source_package"] for entry in libraries.values()},
            "dependency package provenance set mismatch")
    # Source coreutils originals must still match the guest staged bytes.
    for name in PROGRAMS:
        relative = f"usr/bin/{name}"
        require(m245.sha256_file(source / relative) == file_digests[relative],
                f"guest program bytes not from M248 signed sysroot: {relative}")
    return info


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("port", choices=m248.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("extra", type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.port, args.root, args.manifest, args.extra)
    else:
        verify_root(args.port, args.root, args.manifest, args.extra)
        print(f"[M251 EXTRA PROVENANCE VERIFIED] {args.port}", flush=True)


if __name__ == "__main__":
    main()
