#!/usr/bin/env python3
"""M257 reviewed original Debian PID1 shutdown and physical ext4 oracles.

The host signs the nonce INPUT only. The OUTPUT is created solely by the
guest's runlevel-0 action. The independent POST oracle requires natural QEMU
exit, upstream clean ext4 umount, e2fsck, and exact persisted readback.
"""

import argparse
import importlib.util
import re
import secrets
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent


def load(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, HERE / name)
    assert spec and spec.loader
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


m256disk = load("m256-debian-ext4-disk.py", "m256_for_m257_disk")
m255disk, m191 = m256disk.m255disk, m256disk.m191
prov = load("m257-debian-provenance.py", "m257_provenance_for_disk")

INITTAB = (b"id:2:initdefault:\n"
           b"si::sysinit:/etc/m256-guest-sysinit.sh\n"
           b"sv:2:respawn:/etc/m255-guest-service.sh\n"
           b"ct:2:once:/etc/m256-guest-telinit.sh\n"
           b"l3:3:once:/etc/m256-guest-runlevel3.sh\n"
           b"zs:3:once:/etc/m257-guest-telinit.sh\n"
           b"l0:0:wait:/etc/m257-guest-halt.sh\n")
SCRIPTS = {
    "etc/m257-guest-telinit.sh": HERE / "m257-guest-telinit.sh",
    "etc/m257-guest-halt.sh": HERE / "m257-guest-halt.sh",
}
NONCE_INPUT = "etc/m257-nonce-input"
NONCE_OUTPUT = "var/tmp/m255-sysvinit/m257-poweroff-nonce"
NONCE_REGEX = rb"M257-ORIGINAL-DEBIAN-SYSVINIT-EXT4-[0-9a-f]{32}\n"
GUEST_ONLY = (*m256disk.GUEST_ONLY, NONCE_OUTPUT)
READBACK = ("etc/inittab", NONCE_INPUT, "sbin/init", "sbin/m256-mount-run",
            "usr/sbin/telinit", *sorted(m255disk.SCRIPTS),
            *sorted(m256disk.SCRIPTS), *sorted(SCRIPTS),
            *sorted(prov.PROGRAMS))
GUEST_STATE = {
    "var/tmp/m255-sysvinit/sysinit": b"M255-SYSINIT-GUEST\n",
    "var/tmp/m255-sysvinit/first": b"M255-SERVICE-FIRST-GUEST\n",
    "var/tmp/m255-sysvinit/respawn": b"M255-SERVICE-RESPAWN-GUEST\n",
    "var/tmp/m255-sysvinit/runlevel3": b"M256-RUNLEVEL3-GUEST\n",
}


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError("[M257 ORIGINAL DEBIAN DURABLE SHUTDOWN FAIL] " + why)


def stage(root: Path) -> None:
    root = root.resolve()
    (root / "run").mkdir(parents=True, exist_ok=True)
    (root / "etc/inittab").write_bytes(INITTAB)
    for relative, source in SCRIPTS.items():
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(source.read_bytes())
        destination.chmod(0o755)
    nonce_file = root / NONCE_INPUT
    if nonce_file.exists() or nonce_file.is_symlink():
        require(nonce_file.is_file() and not nonce_file.is_symlink() and
                re.fullmatch(NONCE_REGEX, nonce_file.read_bytes()) is not None,
                "existing host nonce input is malformed")
    else:
        nonce_file.write_bytes(
            f"M257-ORIGINAL-DEBIAN-SYSVINIT-EXT4-{secrets.token_hex(16)}\n".encode())
        nonce_file.chmod(0o644)
    require(not (root / NONCE_OUTPUT).exists(),
            "host attempted to preseed guest-generated poweroff nonce")


def verify(port: str, root: Path, disk: Path, base: Path, m251: Path,
           m252: Path, m255: Path, m256: Path, m257: Path, *, after: bool) -> None:
    root, disk, base, m251, m252, m255, m256, m257 = (
        path.resolve() for path in (root, disk, base, m251, m252, m255, m256, m257))
    prov.verify(port, root, base, m251, m252, m255, m256, m257)
    require((root / "etc/inittab").read_bytes() == INITTAB,
            "inittab does not contain the reviewed runlevel 3→0 progression")
    for relative, source in SCRIPTS.items():
        path = root / relative
        require(path.is_file() and not path.is_symlink() and
                path.read_bytes() == source.read_bytes() and
                bool(path.stat().st_mode & 0o111),
                "host-reviewed runlevel script not staged: " + relative)
    nonce = (root / NONCE_INPUT).read_bytes()
    require(re.fullmatch(NONCE_REGEX, nonce) is not None,
            "nonce input is not unique-format 128-bit host random")
    require((root / "run").is_dir() and
            (root / "sbin/m256-mount-run").is_file(),
            "real guest mount(2) helper not staged")
    require(not (root / NONCE_OUTPUT).exists(),
            "guest success result must not come from host staging tree")
    m191.check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m257-debian-durable-", dir=disk.parent) as folder:
        partition = Path(folder) / "root.ext4"
        m191.extract_partition(disk, partition)
        # e2fsck -- no repair, no accepting a dirty mounted filesystem.
        m191.check_fsck(partition)
        for relative in READBACK:
            require(m191.debugfs_cat(partition, "/" + relative) ==
                    (root / relative).read_bytes(),
                    "physical ext4 source/script/host nonce differs: " + relative)
        if not after:
            for relative in GUEST_ONLY:
                require(m191.debugfs_cat(partition, "/" + relative) == b"",
                        "host preseeded runtime guest evidence: " + relative)
        else:
            for relative, expected in GUEST_STATE.items():
                require(m191.debugfs_cat(partition, "/" + relative) == expected,
                        "original PID1 runlevel/service state not persisted: " + relative)
            require(m191.debugfs_cat(partition, "/" + NONCE_OUTPUT) == nonce,
                    "guest nonce missing, mismatched or not durably written")
    if after:
        print(f"[M257 ORIGINAL DEBIAN DURABLE EXT4 POST-NATIVE-QEMU VERIFIED] {port}: "
              "clean fsck, reviewed signed ELF, original init runlevels, guest nonce readback",
              flush=True)
    else:
        print(f"[M257 ORIGINAL DEBIAN PHYSICAL EXT4 PRESEED VERIFIED, "
              f"NOT GUEST PASS] {port}: no guest result; signed binaries and nonce input",
              flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("stage", "before", "after"))
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    if args.mode == "stage":
        require(len(args.paths) == 1, "stage expects root")
        stage(args.paths[0])
    else:
        require(len(args.paths) == 9, "before/after expects port + eight paths")
        port = str(args.paths[0])
        require(port in prov.m248.PORTS, "unsupported ISA")
        verify(port, *(path.resolve() for path in args.paths[1:]),
               after=args.mode == "after")


if __name__ == "__main__":
    main()
