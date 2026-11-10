#!/usr/bin/env python3
"""M255 original sysvinit physical ext4 seed, offline anti-preseed oracle.

No post-QEMU durability success is claimed until real PID1 shutdown and
lwext4 final unmount exist; the diagnostic M255 runner never fakes one.
"""

import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def load(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, HERE / name)
    assert spec and spec.loader
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m252disk = load("m252-debian-ext4-disk.py", "m252_disk_for_m255")
m191 = m252disk.m191
prov = load("m255-debian-provenance.py", "m255_original_init_provenance_disk")
INITTAB = (b"id:2:initdefault:\n"
           b"si::sysinit:/etc/m255-guest-sysinit.sh\n"
           b"sv:2:respawn:/etc/m255-guest-service.sh\n")
SCRIPTS = {
    "etc/m255-guest-sysinit.sh": HERE / "m255-guest-sysinit.sh",
    "etc/m255-guest-service.sh": HERE / "m255-guest-service.sh",
}
GUEST_ONLY = ("var/tmp/m255-sysvinit/sysinit", "var/tmp/m255-sysvinit/first",
              "var/tmp/m255-sysvinit/respawn")


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError("[M255 SYSVINIT EXT4 FAIL] " + why)


def stage(root: Path) -> None:
    (root / "etc/inittab").write_bytes(INITTAB)
    for relative, source in SCRIPTS.items():
        destination = root / relative
        destination.write_bytes(source.read_bytes())
        destination.chmod(0o755)


def verify(port: str, root: Path, disk: Path, base: Path, m251: Path,
           m252: Path, m255: Path) -> None:
    # M252's original provenance/data contract must remain intact, but this
    # SysVinit is a different PID1 and *must not* be asserted as an M252 pass.
    prov.verify_root(port, root, base, m251, m252, m255)
    require((root / "etc/inittab").read_bytes() == INITTAB,
            "non-reviewed inittab in source tree")
    for relative, source in SCRIPTS.items():
        target = root / relative
        require(target.is_file() and not target.is_symlink() and
                target.read_bytes() == source.read_bytes() and
                target.stat().st_mode & 0o111 != 0,
                "reviewed inittab action script missing: " + relative)
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m255-init-oracle-", dir=disk.parent) as tmp:
        partition = Path(tmp) / "root.ext4"
        m191.extract_partition(disk, partition)
        m191.check_fsck(partition)
        for relative in ("etc/inittab", "sbin/init", *SCRIPTS,
                         *sorted(prov.verify_root(port, root, base, m251, m252, m255)["elf_sha256"])):
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    "signed ELF or reviewed init script changed on physical disk: " + relative)
        for relative in GUEST_ONLY:
            require(m191.debugfs_cat(partition, "/" + relative) == b"",
                    "preseeded synthetic guest result: " + relative)
    print(f"[M255 SIGNED SYSVINIT PHYSICAL EXT4 SEED VERIFIED, NOT GUEST PASS] {port}", flush=True)


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "stage":
        stage(Path(sys.argv[2]).resolve())
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", choices=prov.m248.PORTS)
    for name in ("root", "disk", "base", "m251", "m252", "m255"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    verify(args.port, args.root.resolve(), args.disk.resolve(), args.base.resolve(),
           args.m251.resolve(), args.m252.resolve(), args.m255.resolve())


if __name__ == "__main__":
    main()
