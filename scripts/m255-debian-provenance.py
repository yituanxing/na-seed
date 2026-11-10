#!/usr/bin/env python3
"""M255: authenticate the ORIGINAL Debian 13 sysvinit-core PID1, not a shim.

Every ELF byte comes from a SHA256/Size verified .deb in the same signed
trixie Packages/InRelease chain as M248/M251/M252.  Host preparation alone is
not evidence that NaSeed can boot Debian sysvinit.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m252_for_m255", HERE / "m252-debian-provenance.py")
assert spec and spec.loader
m252 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m252)
m251, m245, m248 = m252.m251, m252.m245, m252.m248

SCHEMA = "m255-signed-original-debian-sysvinit-pid1-v1"
PROGRAM = "sbin/init"
# Debian 13 uses merged-/usr: the signed .deb installs this exact ELF at
# /usr/sbin/init, while our native boot manifest requests /sbin/init.  Copy
# the verified bytes without modification into that boot-visible pathname.
SOURCE_PROGRAM = "usr/sbin/init"
PACKAGE = "sysvinit-core"
# M252's shared SONAME allowlist already owns libselinux1/libpcre2/libc6.
# Never silently pull arbitrary DSOs from the host or unrelated repositories.
LIBRARY_PACKAGES = {**m252.LIBRARY_PACKAGES,
                    "libcrypt.so.1": "libcrypt1"}


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError("[M255 ORIGINAL SYSVINIT PROVENANCE FAIL] " + why)


def source_context(port: str, root: Path, base: Path, m251_path: Path,
                   m252_path: Path):
    _source, apt, m248_info, m251_info = m252.source_context(port, root, base, m251_path)
    m252_info = m252.verify_root(port, root, base, m251_path, m252_path)
    return apt, m248_info, m251_info, m252_info


def init_elf(path: Path, port: str, *, executable: bool) -> dict:
    # Reuse M252's precise target-machine, PIE, PT_INTERP checks, but M255
    # allows only its reviewed set of extra signed SONAMEs.
    import re
    _, machine, interpreter = m248.PORTS[port]
    hdr = m245.run("readelf", "-h", str(path), capture=True)
    require("ELF64" in hdr and machine in hdr and
            re.search(r"Type:\s+DYN\s", hdr) is not None,
            f"unexpected ELF class/ISA/type: {path}")
    dynamic = m245.run("readelf", "-d", str(path), capture=True)
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic)
    require(len(needed) == len(set(needed)) and
            "libc.so.6" in needed and
            all(x in {"libc.so.6", Path(interpreter).name, *LIBRARY_PACKAGES}
                for x in needed), f"unreviewed sysvinit ELF dependency: {path}: {needed}")
    result = {"dt_needed": needed}
    if executable:
        require(m245.elf_interpreter(path) == interpreter,
                "sysvinit is not the original native Debian glibc PT_INTERP")
        result["interpreter"] = interpreter
    return result


def prepare(port: str, root: Path, base: Path, m251_path: Path,
            m252_path: Path, extra_path: Path) -> dict:
    root, base, m251_path, m252_path, extra_path = map(
        Path.resolve, (root, base, m251_path, m252_path, extra_path))
    apt, signed_base, m251_info, m252_info = source_context(
        port, root, base, m251_path, m252_path)
    triplet, _, interpreter = m248.PORTS[port]
    record = m252.package_record(apt, port, PACKAGE)
    info = {
        "schema": SCHEMA,
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "architecture": port,
        "distribution": "Debian 13 trixie",
        "inrelease_sha256": signed_base["inrelease_sha256"],
        "base_manifest_sha256": m245.sha256_file(base),
        "m251_manifest_sha256": m245.sha256_file(m251_path),
        "m252_manifest_sha256": m245.sha256_file(m252_path),
        "program_packages": {PACKAGE: m252.package_summary(record)},
        "dependency_packages": {}, "programs": {}, "libraries": {},
        "reused_libraries": {}, "elf_sha256": {},
    }
    with tempfile.TemporaryDirectory(prefix="m255-signed-sysvinit-") as tmp:
        stage = Path(tmp)
        extracted = m252.fetch_and_extract(record, base, stage / PACKAGE)
        original = m248.original_regular_file(extracted, Path(SOURCE_PROGRAM))
        props = init_elf(original, port, executable=True)
        digest = m251.copy_signed_bytes(original, root / PROGRAM)
        info["programs"][PROGRAM] = {"source_package": PACKAGE,
                                      "sha256": digest, **props}
        info["elf_sha256"][PROGRAM] = digest
        pending = list(props["dt_needed"])
        inherited = {**m251_info["libraries"], **m252_info["libraries"]}
        while pending:
            soname = pending.pop(0)
            if soname in ("libc.so.6", Path(interpreter).name):
                continue
            require(soname in LIBRARY_PACKAGES,
                    "unknown dependency; review signed package first: " + soname)
            relative = f"usr/lib/{triplet}/{soname}"
            if relative in inherited:
                require(m245.sha256_file(root / relative) == inherited[relative]["sha256"],
                        f"inherited signed library bytes modified: {relative}")
                info["reused_libraries"][relative] = inherited[relative]["sha256"]
                continue
            if relative in info["libraries"]:
                continue
            library_package = LIBRARY_PACKAGES[soname]
            if library_package not in info["dependency_packages"]:
                dep = m252.package_record(apt, port, library_package)
                m252.fetch_and_extract(dep, base, stage / library_package)
                info["dependency_packages"][library_package] = m252.package_summary(dep)
            signed = m251.locate_original_library(stage / library_package, triplet, soname)
            lib_props = init_elf(signed, port, executable=False)
            digest = m251.copy_signed_bytes(signed, root / relative)
            info["libraries"][relative] = {
                "source_package": library_package, "sha256": digest, **lib_props}
            info["elf_sha256"][relative] = digest
            pending.extend(lib_props["dt_needed"])
    verify_root(port, root, base, m251_path, m252_path, info)
    rendered = json.dumps(info, indent=2, sort_keys=True) + "\n"
    if extra_path.exists():
        require(extra_path.read_text(encoding="utf-8") == rendered,
                "would overwrite existing sysvinit provenance")
    else:
        extra_path.write_text(rendered, encoding="utf-8")
    print(f"[M255 SIGNED ORIGINAL SYSVINIT HOST PREPARED, NOT GUEST PASS] {port} "
          f"sysvinit-core ELF, {len(info['libraries'])} new DSOs, "
          f"{len(info['reused_libraries'])} inherited signed DSOs", flush=True)
    return info


def verify_root(port: str, root: Path, base: Path, m251_path: Path,
                m252_path: Path, extra: dict | Path) -> dict:
    root, base, m251_path, m252_path = map(
        Path.resolve, (root, base, m251_path, m252_path))
    info = (json.loads(extra.read_text(encoding="utf-8"))
            if isinstance(extra, Path) else extra)
    apt, signed_base, m251_info, m252_info = source_context(
        port, root, base, m251_path, m252_path)
    require(info.get("schema") == SCHEMA and info.get("architecture") == port and
            info.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" and
            info.get("inrelease_sha256") == signed_base["inrelease_sha256"] and
            info.get("base_manifest_sha256") == m245.sha256_file(base) and
            info.get("m251_manifest_sha256") == m245.sha256_file(m251_path) and
            info.get("m252_manifest_sha256") == m245.sha256_file(m252_path),
            "unsigned or inconsistent parent manifests")
    programs, libraries, reused = (info.get(key) for key in
                                   ("programs", "libraries", "reused_libraries"))
    packages, deps, hashes = (info.get(key) for key in
                              ("program_packages", "dependency_packages", "elf_sha256"))
    require(all(isinstance(v, dict) for v in
                (programs, libraries, reused, packages, deps, hashes)) and
            set(programs) == {PROGRAM} and set(packages) == {PACKAGE} and
            set(hashes) == {PROGRAM} | set(libraries),
            "missing/extra sysvinit signed ELF or metadata")
    signed_debs = {**packages, **deps}
    for package, details in signed_debs.items():
        require(details == m252.package_summary(m252.package_record(apt, port, package)),
                "signed Packages identity mismatch: " + package)
        archive = base.parent / "debs" / Path(details["pool"]).name
        require(archive.is_file() and not archive.is_symlink() and
                m245.sha256_file(archive) == details["sha256"] and
                archive.stat().st_size == details["size"],
                "signed .deb changed: " + package)
    with tempfile.TemporaryDirectory(prefix="m255-sysvinit-verify-") as tmp:
        extracted = {}
        for package, details in signed_debs.items():
            target = Path(tmp) / package
            m245.run("dpkg-deb", "-x", str(base.parent / "debs" /
                                      Path(details["pool"]).name), str(target))
            extracted[package] = target
        inherited = {**m251_info["libraries"], **m252_info["libraries"]}
        require(set(reused).issubset(inherited), "new library is falsely inherited")
        for relative, digest in reused.items():
            require(digest == inherited[relative]["sha256"] and
                    m245.sha256_file(root / relative) == digest,
                    "tampered inherited signed DSO: " + relative)
        for relative, details in {**programs, **libraries}.items():
            package = details["source_package"]
            signed = (m248.original_regular_file(extracted[package], Path(SOURCE_PROGRAM))
                      if relative == PROGRAM else
                      m251.locate_original_library(extracted[package],
                                                    m248.PORTS[port][0], Path(relative).name))
            props = init_elf(signed, port, executable=relative == PROGRAM)
            digest = m245.sha256_file(signed)
            require(details == {"source_package": package, "sha256": digest, **props} and
                    hashes[relative] == digest and
                    m245.sha256_file(root / relative) == digest,
                    "original signed ELF bytes/ABI differ: " + relative)
        triplet, _, interpreter = m248.PORTS[port]
        needed = set(programs[PROGRAM]["dt_needed"])
        for details in libraries.values():
            needed.update(details["dt_needed"])
        required = {f"usr/lib/{triplet}/{lib}" for lib in needed
                    if lib not in ("libc.so.6", Path(interpreter).name)}
        require(required == set(reused) | set(libraries) and
                set(deps) == {d["source_package"] for d in libraries.values()},
                "unclosed/extra signed DT_NEEDED dependencies")
    return info


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("port", choices=m248.PORTS)
    for name in ("root", "base", "m251", "m252", "extra"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.port, args.root, args.base, args.m251, args.m252, args.extra)
    else:
        verify_root(args.port, args.root, args.base, args.m251, args.m252, args.extra)


if __name__ == "__main__":
    main()
