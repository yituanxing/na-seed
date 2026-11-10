#!/usr/bin/env python3
"""M257: retain original signed Debian SysVinit halt and coreutils sync.

The same Debian Trixie InRelease/Packages chain used by M248--M256 owns both
binary archives. This host-side proof never substitutes for guest execution.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("m256_for_m257", HERE / "m256-debian-provenance.py")
assert spec and spec.loader
m256 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m256)
m255, m251, m245, m248 = m256.m255, m256.m251, m256.m245, m256.m248

SCHEMA = "m257-signed-debian-sysvinit-natural-shutdown-v1"
PROGRAMS = {"usr/sbin/halt": "sysvinit-core", "usr/bin/sync": "coreutils"}


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError("[M257 SIGNED SHUTDOWN FAIL] " + why)


def original_sources(port: str, base: Path, m255_path: Path, extracted: Path):
    archive = m256.verified_original(port, base, m255_path)
    sysvinit_dir = extracted / "sysvinit-core"
    m245.run("dpkg-deb", "-x", str(archive), str(sysvinit_dir))
    base_info = json.loads(base.read_text(encoding="utf-8"))
    core_dir = m251.verified_coreutils_extract(base, base_info, extracted)
    return {
        "usr/sbin/halt": m248.original_regular_file(sysvinit_dir, Path("usr/sbin/halt")),
        "usr/bin/sync": m248.original_regular_file(core_dir, Path("usr/bin/sync")),
    }


def records(port: str, root: Path, base: Path, m255_path: Path,
            *, stage: bool) -> dict:
    _, _, interp = m248.PORTS[port]
    inherited = {**m251.LIBRARY_PACKAGES, **m255.m252.LIBRARY_PACKAGES,
                 **m255.LIBRARY_PACKAGES}
    with tempfile.TemporaryDirectory(prefix="m257-signed-shutdown-") as folder:
        original = original_sources(port, base, m255_path, Path(folder))
        programs = {}
        for relative, package in PROGRAMS.items():
            source = original[relative]
            props = m255.init_elf(source, port, executable=True)
            for soname in props["dt_needed"]:
                if soname in ("libc.so.6", Path(interp).name):
                    continue
                require(soname in inherited, "unreviewed ELF dependency: " + soname)
                library = root / f"usr/lib/{m248.PORTS[port][0]}/{soname}"
                require(library.is_file() and not library.is_symlink(),
                        "signed ELF dependency not present in root: " + soname)
            digest = m245.sha256_file(source)
            target = root / relative
            if stage:
                require(m251.copy_signed_bytes(source, target) == digest,
                        "signed source changed while staging: " + relative)
            require(target.is_file() and not target.is_symlink() and
                    m245.sha256_file(target) == digest,
                    "original signed ELF changed on root: " + relative)
            programs[relative] = {"source_package": package,
                                  "sha256": digest, "elf": props}
        return programs


def verify(port: str, root: Path, base: Path, m251_path: Path,
           m252_path: Path, m255_path: Path, m256_path: Path,
           m257_path: Path) -> dict:
    m256.verify(port, root, base, m251_path, m252_path, m255_path, m256_path)
    info = json.loads(m257_path.read_text(encoding="utf-8"))
    expected = {
        "schema": SCHEMA,
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "architecture": port,
        "m256_manifest_sha256": m245.sha256_file(m256_path),
        "programs": records(port, root, base, m255_path, stage=False),
    }
    require(info == expected, "signed halt/sync manifest or source bytes differ")
    return info


def prepare(port: str, root: Path, base: Path, m251_path: Path,
            m252_path: Path, m255_path: Path, m256_path: Path,
            m257_path: Path) -> None:
    m256.verify(port, root, base, m251_path, m252_path, m255_path, m256_path)
    info = {
        "schema": SCHEMA,
        "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
        "architecture": port,
        "m256_manifest_sha256": m245.sha256_file(m256_path),
        "programs": records(port, root, base, m255_path, stage=True),
    }
    rendered = json.dumps(info, sort_keys=True, indent=2) + "\n"
    if m257_path.exists():
        require(m257_path.read_text(encoding="utf-8") == rendered,
                "refuse to replace existing signed source metadata")
    else:
        m257_path.write_text(rendered, encoding="utf-8")
    verify(port, root, base, m251_path, m252_path, m255_path, m256_path, m257_path)
    print(f"[M257 ORIGINAL SIGNED HALT+SYNC HOST PREPARED, NOT QEMU PASS] {port}",
          flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "verify"))
    parser.add_argument("port", choices=m248.PORTS)
    for name in ("root", "base", "m251", "m252", "m255", "m256", "m257"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    paths = [getattr(args, name).resolve() for name in
             ("root", "base", "m251", "m252", "m255", "m256", "m257")]
    if args.mode == "prepare":
        prepare(args.port, *paths)
    else:
        verify(args.port, *paths)


if __name__ == "__main__":
    main()
