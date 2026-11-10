#!/usr/bin/env python3
"""M256: original signed Debian telinit control ELF (not a host shim)."""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m255_signed_for_m256", HERE / "m255-debian-provenance.py")
assert spec and spec.loader
m255 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m255)
m252, m251, m245, m248 = m255.m252, m255.m251, m255.m245, m255.m248

SCHEMA = "m256-signed-debian-original-sysvinit-initctl-v1"
SOURCE = "usr/sbin/telinit"
TARGET = "usr/sbin/telinit"


def require(ok: bool, msg: str) -> None:
    if not ok:
        raise ValueError("[M256 SIGNED DEBIAN INITCTL FAIL] " + msg)


def verified_original(port: str, base: Path, m255_manifest: Path):
    # Reopen and verify the exact sysvinit-core deb already authenticated by
    # M255. No unpinned apt download and no independently chosen host DSO.
    prior = json.loads(m255_manifest.read_text(encoding="utf-8"))
    summary = prior["program_packages"][m255.PACKAGE]
    archive = base.parent / "debs" / Path(summary["pool"]).name
    require(archive.is_file() and not archive.is_symlink() and
            m245.sha256_file(archive) == summary["sha256"] and
            archive.stat().st_size == summary["size"],
            "signed sysvinit-core .deb changed")
    return archive


def verify(port: str, root: Path, base: Path, m251_path: Path,
           m252_path: Path, m255_path: Path, m256_path: Path) -> dict:
    info = json.loads(m256_path.read_text(encoding="utf-8"))
    prior = m255.verify_root(port, root, base, m251_path, m252_path, m255_path)
    require(info.get("schema") == SCHEMA and info.get("architecture") == port and
            info.get("status") == "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS" and
            info.get("m255_manifest_sha256") == m245.sha256_file(m255_path) and
            info.get("source_path") == SOURCE and info.get("target_path") == TARGET,
            "staged control ELF has mismatched parent identity")
    archive = verified_original(port, base, m255_path)
    with tempfile.TemporaryDirectory(prefix="m256-signed-verify-") as folder:
        extracted = Path(folder)
        m245.run("dpkg-deb", "-x", str(archive), str(extracted))
        original = m248.original_regular_file(extracted, Path(SOURCE))
        props = m255.init_elf(original, port, executable=True)
        digest = m245.sha256_file(original)
        require(info.get("program_sha256") == digest and
                info.get("program_elf") == props and
                m245.sha256_file(root / TARGET) == digest,
                "original signed telinit ELF differs on guest root")
        triplet, _, interp = m248.PORTS[port]
        closure = {f"usr/lib/{triplet}/{soname}" for soname in props["dt_needed"]
                   if soname not in ("libc.so.6", Path(interp).name)}
        inherited = {**m251.LIBRARY_PACKAGES, **m252.LIBRARY_PACKAGES,
                     **m255.LIBRARY_PACKAGES}
        require(all((root / path).is_file() and Path(path).name in inherited
                    for path in closure), "telinit introduces unreviewed dependencies")
    return info


def prepare(port: str, root: Path, base: Path, m251_path: Path,
            m252_path: Path, m255_path: Path, m256_path: Path) -> None:
    prior = m255.verify_root(port, root, base, m251_path, m252_path, m255_path)
    archive = verified_original(port, base, m255_path)
    with tempfile.TemporaryDirectory(prefix="m256-signed-telinit-") as folder:
        extracted = Path(folder)
        m245.run("dpkg-deb", "-x", str(archive), str(extracted))
        original = m248.original_regular_file(extracted, Path(SOURCE))
        props = m255.init_elf(original, port, executable=True)
        digest = m251.copy_signed_bytes(original, root / TARGET)
    info = {"schema": SCHEMA, "architecture": port,
            "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
            "m255_manifest_sha256": m245.sha256_file(m255_path),
            "source_path": SOURCE, "target_path": TARGET,
            "program_sha256": digest, "program_elf": props}
    rendered = json.dumps(info, indent=2, sort_keys=True) + "\n"
    if m256_path.exists():
        require(m256_path.read_text(encoding="utf-8") == rendered,
                "refuse to replace existing signed telinit evidence")
    else:
        m256_path.write_text(rendered, encoding="utf-8")
    verify(port, root, base, m251_path, m252_path, m255_path, m256_path)
    print(f"[M256 ORIGINAL SIGNED TELINIT HOST PREPARED, NOT GUEST PASS] {port}"
          f" {digest}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("port", choices=m248.PORTS)
    for name in ("root", "base", "m251", "m252", "m255", "m256"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    paths = [Path(getattr(args, name)).resolve() for name in
             ("root", "base", "m251", "m252", "m255", "m256")]
    if args.mode == "prepare":
        prepare(args.port, *paths)
    else:
        verify(args.port, *paths)


if __name__ == "__main__":
    main()
