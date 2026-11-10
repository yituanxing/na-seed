#!/usr/bin/env python3
"""Physical ext4 preflight for original Debian initctl/runlevel diagnostic.

M256 stays a diagnostic until guest-initiated shutdown, clean ext4 unmount,
native QEMU exit, and an independent AFTER disk oracle all pass.
"""

import argparse
import importlib.util
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent


def load(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, HERE / name)
    assert spec and spec.loader
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m255disk = load("m255-debian-ext4-disk.py", "m255disk_for_m256")
m191 = m255disk.m191
prov = load("m256-debian-provenance.py", "m256_original_telinit_for_disk")

INITTAB = (b"id:2:initdefault:\n"
           b"si::sysinit:/etc/m256-guest-sysinit.sh\n"
           b"sv:2:respawn:/etc/m255-guest-service.sh\n"
           b"ct:2:once:/etc/m256-guest-telinit.sh\n"
           b"l3:3:once:/etc/m256-guest-runlevel3.sh\n")
SCRIPTS = {
    "etc/m256-guest-sysinit.sh": HERE / "m256-guest-sysinit.sh",
    "etc/m256-guest-telinit.sh": HERE / "m256-guest-telinit.sh",
    "etc/m256-guest-runlevel3.sh": HERE / "m256-guest-runlevel3.sh",
}
GUEST_ONLY = (*m255disk.GUEST_ONLY, "var/tmp/m255-sysvinit/runlevel3")


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError("[M256 ORIGINAL INITCTL PHYSICAL EXT4 FAIL] " + why)


def stage(root: Path) -> None:
    (root / "run").mkdir(parents=True, exist_ok=True)
    (root / "etc/inittab").write_bytes(INITTAB)
    for relative, source in SCRIPTS.items():
        destination = root / relative
        destination.write_bytes(source.read_bytes())
        destination.chmod(0o755)


def verify(port: str, root: Path, disk: Path, base: Path, m251: Path,
           m252: Path, m255: Path, m256: Path) -> None:
    prov.verify(port, root, base, m251, m252, m255, m256)
    require((root / "etc/inittab").read_bytes() == INITTAB,
            "root inittab not reviewed M256 runlevel actions")
    for relative, source in SCRIPTS.items():
        path = root / relative
        require(path.is_file() and path.read_bytes() == source.read_bytes() and
                path.stat().st_mode & 0o111, "incorrect guest action " + relative)
    require((root / "sbin/m256-mount-run").is_file() and
            (root / "run").is_dir(), "missing distinct native tmpfs-mount setup")
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m256-initctl-physical-", dir=disk.parent) as folder:
        partition = Path(folder) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        for relative in ("etc/inittab", "sbin/init", "sbin/m256-mount-run",
                         "usr/sbin/telinit", *sorted(m255disk.SCRIPTS),
                         *sorted(SCRIPTS)):
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    "signed binary or reviewed guest script mismatch: " + relative)
        for relative in GUEST_ONLY:
            require(m191.debugfs_cat(partition, "/" + relative) == b"",
                    "preseeded guest success artifact: " + relative)
    print(f"[M256 SIGNED ORIGINAL INITCTL PHYSICAL EXT4 SEED VERIFIED, "
          f"NOT GUEST PASS] {port}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("stage", "before"))
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    if args.mode == "stage":
        require(len(args.paths) == 1, "stage needs root")
        stage(args.paths[0].resolve())
    else:
        require(len(args.paths) == 8, "before needs port plus seven paths")
        port = str(args.paths[0])
        require(port in prov.m248.PORTS, "unsupported ISA")
        verify(port, *(p.resolve() for p in args.paths[1:]))


if __name__ == "__main__":
    main()
