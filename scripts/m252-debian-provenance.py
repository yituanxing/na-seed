#!/usr/bin/env python3
"""M252 signed Debian 13/glibc grep, sed and findutils provenance closure.

Authenticates three new original Debian packages against the precise signed
APT trixie index staged by M248. M248/M251 manifests and already authenticated
guest ELF bytes are immutable. Host preparation is NOT a native guest PASS.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import tempfile


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "m251_signed_debian_for_m252", HERE / "m251-debian-provenance.py")
assert SPEC and SPEC.loader
m251 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m251)
m248, m245 = m251.m248, m251.m245

PROGRAM_PACKAGES = {"grep": "grep", "sed": "sed", "find": "findutils"}
LIBRARY_PACKAGES = dict(m251.LIBRARY_PACKAGES)
SCHEMA = "m252-signed-debian-trixie-grep-sed-find-v1"


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise ValueError("[M252 SIGNED PROVENANCE FAIL] " + reason)


def read_json(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(data, dict), "expected JSON object at " + str(path))
    return data


def source_context(port: str, root: Path, base_path: Path,
                   m251_path: Path) -> tuple[Path, Path, dict, dict]:
    root, base_path, m251_path = (x.resolve() for x in (root, base_path, m251_path))
    source, apt, base = m251.source_context(port, base_path)
    # Do not merely trust M251 manifest assertions; compare every installed
    # coreutils ELF/DSO byte and its native glibc ABI against signed source.
    inherited = m251.verify_root(port, root, base_path, m251_path)
    require(inherited.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" and
            inherited.get("architecture") == port,
            "unauthenticated M251 parent manifest")
    return source, apt, base, inherited


def elf_properties(path: Path, port: str, *, executable: bool) -> dict:
    _, machine, interpreter = m248.PORTS[port]
    header = m245.run("readelf", "-h", str(path), capture=True)
    require("ELF64" in header and machine in header and
            re.search(r"Type:\s+DYN\s", header) is not None,
            f"unexpected native ELF class/ISA/type: {path}")
    dyn = m245.run("readelf", "-d", str(path), capture=True)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dyn)
    require(len(needed) == len(set(needed)), f"duplicate DT_NEEDED: {path}")
    require("libc.so.6" in needed and all(
        name in {"libc.so.6", Path(interpreter).name, *LIBRARY_PACKAGES} for name in needed),
        f"unreviewed glibc DT_NEEDED closure: {path}: {needed}")
    info = {"dt_needed": needed}
    if executable:
        require(m245.elf_interpreter(path) == interpreter,
                f"unexpected original glibc PT_INTERP: {path}")
        info["interpreter"] = interpreter
    return info


def package_record(apt: Path, port: str, name: str) -> dict:
    # This is the already APT signature-verified index scoped to trixie/main.
    # M251 safely handles the Debian version/path grammar, including '~'.
    return m251.signed_dependency_record(apt, port, name)


def fetch_and_extract(record: dict, manifest: Path, dest: Path) -> Path:
    archive = manifest.resolve().parent / "debs" / Path(record["Filename"]).name
    m245.fetch_verified_deb(record, archive)
    require(archive.is_file() and not archive.is_symlink() and
            m245.sha256_file(archive) == record["SHA256"] and
            archive.stat().st_size == int(record["Size"]),
            "downloaded package no longer matches signed Packages SHA/Size")
    m245.run("dpkg-deb", "-x", str(archive), str(dest))
    return dest


def package_summary(record: dict) -> dict:
    return {"version": record["Version"], "sha256": record["SHA256"],
            "size": int(record["Size"]), "pool": record["Filename"]}


def prepare(port: str, root: Path, base_path: Path,
            m251_path: Path, extra_path: Path) -> dict:
    root, base_path, m251_path, extra_path = (p.resolve() for p in
                                               (root, base_path, m251_path, extra_path))
    _, apt, base, inherited = source_context(port, root, base_path, m251_path)
    triplet, _, interpreter = m248.PORTS[port]
    info = {
        "schema": SCHEMA,
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "distribution": "Debian 13 trixie",
        "architecture": port,
        "base_manifest_sha256": m245.sha256_file(base_path),
        "m251_manifest_sha256": m245.sha256_file(m251_path),
        "inrelease_sha256": base["inrelease_sha256"],
        "programs": {}, "libraries": {}, "reused_libraries": {},
        "program_packages": {}, "dependency_packages": {},
        "elf_sha256": {},
    }
    with tempfile.TemporaryDirectory(prefix="m252-signed-extract-") as folder:
        stage = Path(folder)
        pending = []
        for program, package in PROGRAM_PACKAGES.items():
            record = package_record(apt, port, package)
            extracted = fetch_and_extract(record, base_path, stage / package)
            relative = f"usr/bin/{program}"
            signed = m248.original_regular_file(extracted, Path(relative))
            props = elf_properties(signed, port, executable=True)
            digest = m251.copy_signed_bytes(signed, root / relative)
            info["programs"][relative] = {"source_package": package,
                                           "sha256": digest, **props}
            info["program_packages"][package] = package_summary(record)
            info["elf_sha256"][relative] = digest
            pending.extend(props["dt_needed"])

        while pending:
            soname = pending.pop(0)
            if soname in ("libc.so.6", Path(interpreter).name):
                continue
            require(soname in LIBRARY_PACKAGES,
                    f"unreviewed original Debian SONAME: {soname}")
            relative = f"usr/lib/{triplet}/{soname}"
            if relative in inherited["libraries"]:
                require((root / relative).is_file() and
                        m245.sha256_file(root / relative) ==
                        inherited["libraries"][relative]["sha256"],
                        f"inherited original signed DSO changed: {soname}")
                info["reused_libraries"][relative] = inherited["libraries"][relative]["sha256"]
                continue
            if relative in info["libraries"]:
                continue
            package = LIBRARY_PACKAGES[soname]
            if package not in info["dependency_packages"]:
                record = package_record(apt, port, package)
                fetch_and_extract(record, base_path, stage / package)
                info["dependency_packages"][package] = package_summary(record)
            signed = m251.locate_original_library(stage / package, triplet, soname)
            props = elf_properties(signed, port, executable=False)
            digest = m251.copy_signed_bytes(signed, root / relative)
            info["libraries"][relative] = {"source_package": package,
                                            "sha256": digest, **props}
            info["elf_sha256"][relative] = digest
            pending.extend(props["dt_needed"])
    verify_root(port, root, base_path, m251_path, info)
    extra_path.parent.mkdir(parents=True, exist_ok=True)
    rendered = json.dumps(info, indent=2, sort_keys=True) + "\n"
    if extra_path.exists():
        require(extra_path.read_text(encoding="utf-8") == rendered,
                "existing M252 signed manifest changed; no silent overwrite")
    else:
        extra_path.write_text(rendered, encoding="utf-8")
    print(f"[M252 SIGNED HOST PREPARED, NOT NATIVE QEMU PASS] {port} "
          f"3 new original Debian ELF, {len(info['libraries'])} new signed DSOs; "
          f"{len(info['reused_libraries'])} inherited signed DSOs", flush=True)
    return info


def verify_root(port: str, root: Path, base_path: Path, m251_path: Path,
                extra: dict | Path) -> dict:
    root, base_path, m251_path = (p.resolve() for p in (root, base_path, m251_path))
    info = read_json(extra) if isinstance(extra, Path) else extra
    _, apt, base, inherited = source_context(port, root, base_path, m251_path)
    require(info.get("schema") == SCHEMA and info.get("architecture") == port and
            info.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" and
            info.get("inrelease_sha256") == base["inrelease_sha256"] and
            info.get("base_manifest_sha256") == m245.sha256_file(base_path) and
            info.get("m251_manifest_sha256") == m245.sha256_file(m251_path),
            "M252 identity is not tied to both signed parent manifests")
    programs = info.get("programs")
    libs = info.get("libraries")
    reused = info.get("reused_libraries")
    packages = info.get("program_packages")
    deps = info.get("dependency_packages")
    hashes = info.get("elf_sha256")
    require(all(isinstance(v, dict) for v in (programs, libs, reused, packages, deps, hashes)),
            "invalid M252 signed metadata sets")
    require(set(programs) == {f"usr/bin/{name}" for name in PROGRAM_PACKAGES} and
            set(packages) == set(PROGRAM_PACKAGES.values()) and
            set(hashes) == set(programs) | set(libs) and
            not (set(libs) & set(inherited["libraries"])) and
            set(reused).issubset(set(inherited["libraries"])),
            "missing/unexpected original grep/sed/find/DSO metadata")
    # Validate each .deb's signed-index record and precise on-disk archive
    # SHA/Size. A fabricated manifest cannot replace Debian's Packages data.
    for package, data in {**packages, **deps}.items():
        require(data == package_summary(package_record(apt, port, package)),
                f"package {package} mismatched APT-authenticated index")
        archive = base_path.parent / "debs" / Path(data["pool"]).name
        require(archive.is_file() and not archive.is_symlink() and
                m245.sha256_file(archive) == data["sha256"] and
                archive.stat().st_size == data["size"],
                f"signed Debian .deb package bytes changed: {package}")
    required_sonames = set()
    for relative, details in {**programs, **libs}.items():
        require(details.get("sha256") == hashes.get(relative),
                f"incorrect M252 original ELF digest mapping: {relative}")
        path = root / relative
        require(path.is_file() and not path.is_symlink() and
                path.resolve().is_relative_to(root) and
                m245.sha256_file(path) == hashes[relative],
                f"original Debian ELF modified in guest root: {relative}")
        props = elf_properties(path, port, executable=relative in programs)
        require(all(details.get(key) == value for key, value in props.items()),
                f"original Debian ELF ABI mutated: {relative}")
        required_sonames.update(props["dt_needed"])
        if relative in programs:
            require(details.get("source_package") == PROGRAM_PACKAGES[Path(relative).name],
                    f"wrong package origin for original Debian ELF: {relative}")
        else:
            require(details.get("source_package") ==
                    LIBRARY_PACKAGES.get(Path(relative).name),
                    f"wrong authenticated DSO package: {relative}")
    expected_inherited = {f"usr/lib/{m248.PORTS[port][0]}/{soname}"
                          for soname in required_sonames if
                          f"usr/lib/{m248.PORTS[port][0]}/{soname}" in inherited["libraries"]}
    expected_new = {f"usr/lib/{m248.PORTS[port][0]}/{soname}"
                    for soname in required_sonames
                    if soname in LIBRARY_PACKAGES} - expected_inherited
    require(set(libs) == expected_new and set(reused) == expected_inherited and
            all(reused[path] == inherited["libraries"][path]["sha256"]
                for path in reused) and
            set(deps) == {v["source_package"] for v in libs.values()},
            "unclosed signed dependency/reused DSO set")
    # Every ELF digest is independently tied to the extraction of its signed
    # .deb; otherwise a fabricated ELF and a self-consistent manifest could
    # be substituted while the genuine .deb merely sat alongside it.
    with tempfile.TemporaryDirectory(prefix="m252-verify-original-") as folder:
        extracts = {}
        for package, package_info in {**packages, **deps}.items():
            archive = base_path.parent / "debs" / Path(package_info["pool"]).name
            extracted = Path(folder) / package
            m245.run("dpkg-deb", "-x", str(archive), str(extracted))
            extracts[package] = extracted
        for program, package in PROGRAM_PACKAGES.items():
            relative = f"usr/bin/{program}"
            original = m248.original_regular_file(extracts[package], Path(relative))
            require(m245.sha256_file(original) == hashes[relative],
                    f"not original signed Debian {package} ELF: {relative}")
        triplet = m248.PORTS[port][0]
        for relative, details in libs.items():
            original = m251.locate_original_library(
                extracts[details["source_package"]], triplet,
                Path(relative).name)
            require(m245.sha256_file(original) == hashes[relative],
                    f"not original signed Debian DSO: {relative}")
    return info


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("port", choices=m248.PORTS)
    parser.add_argument("root", type=Path)
    parser.add_argument("base_manifest", type=Path)
    parser.add_argument("m251_manifest", type=Path)
    parser.add_argument("m252_manifest", type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.port, args.root, args.base_manifest,
                args.m251_manifest, args.m252_manifest)
    else:
        verify_root(args.port, args.root, args.base_manifest,
                    args.m251_manifest, args.m252_manifest)
        print(f"[M252 SIGNED ROOT VERIFIED] {args.port}", flush=True)


if __name__ == "__main__":
    main()
