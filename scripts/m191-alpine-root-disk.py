#!/usr/bin/env python3
"""Build/verify an independent physical Alpine ext4 root disk for NaSeed.

The only userspace inputs are the official SHA-checked Alpine minirootfs and
signature-verified native APK packages installed by rootfs/alpine/build.sh.
This is HOST preparation, not evidence that Alpine init has booted in NaSeed.
"""

from collections import deque
import hashlib
import os
from pathlib import Path
import posixpath
import shutil
import struct
import subprocess
import sys
import tempfile


ARCH_MACHINE = {"amd64": 62, "riscv64": 243, "arm64": 183}
SECTOR = 512
DISK_SIZE = 256 * 1024 * 1024
PART_START = 4096  # 2 MiB, separate partition and DOS MBR
PART_SECTORS = DISK_SIZE // SECTOR - PART_START
CRITICAL = (
    "/etc/alpine-release", "/etc/apk/world", "/etc/apk/repositories",
    "/etc/inittab", "/bin/busybox", "/sbin/init", "/sbin/openrc",
)
READBACK = ("/etc/alpine-release", "/etc/apk/world", "/etc/apk/repositories",
            "/etc/inittab")
SERIAL_GETTY = "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100"
LEGACY_GETTYS = frozenset(f"tty{number}" for number in range(1, 7))


def fail(message: str) -> None:
    raise ValueError("[M191 HOST FAIL] " + message)


def guest_path(root: Path, name: str) -> Path:
    """Resolve guest absolute symlinks in the guest root, never host `/`."""
    if not name.startswith("/"):
        fail("non-absolute guest path: " + name)
    remaining = deque(name.split("/")[1:])
    parts: list[str] = []
    links = 0
    while remaining:
        item = remaining.popleft()
        if item in ("", "."):
            continue
        if item == "..":
            if not parts:
                fail("guest symlink escapes root: " + name)
            parts.pop()
            continue
        candidate = root.joinpath(*parts, item)
        if candidate.is_symlink():
            links += 1
            if links > 32:
                fail("guest symlink loop: " + name)
            link = os.readlink(candidate)
            if link.startswith("/"):
                parts.clear()
            remaining.extendleft(reversed(link.split("/")))
        else:
            parts.append(item)
    return root.joinpath(*parts)


def configure_serial_inittab(root: Path) -> None:
    """Adapt only the getty device names; leave signed Alpine init/OpenRC intact.

    NaSeed has a real cross-ISA serial /dev/ttyS0 but no Linux virtual
    terminals /dev/tty1..6. BusyBox init would otherwise spawn six failed
    gettys indefinitely. This is a host rootfs configuration overlay, never
    a replacement guest PID1 or a fake kernel console.
    """
    path = guest_path(root, "/etc/inittab")
    if not path.is_file():
        fail("official Alpine /etc/inittab missing")
    original = path.read_text()
    lines = original.splitlines()
    selected = []
    removed = 0
    serial = 0
    for line in lines:
        fields = line.split(":", 3)
        if len(fields) == 4 and fields[0] in LEGACY_GETTYS and fields[2] == "respawn":
            removed += 1
            continue
        if len(fields) == 4 and fields[0] == "ttyS0" and fields[2] == "respawn":
            if line != SERIAL_GETTY:
                fail("conflicting Alpine serial getty configuration")
            serial += 1
        selected.append(line)
    if serial > 1 or not removed and serial != 1:
        fail("unexpected Alpine getty layout; refusing implicit init replacement")
    if not serial:
        selected.append(SERIAL_GETTY)
    updated = "\n".join(selected) + "\n"
    if updated != original:
        path.write_text(updated)
    path.chmod(0o644)


def configure_tree(root: Path) -> None:
    root = root.resolve(strict=True)
    if os.geteuid() != 0:
        fail("rootfs configuration must preserve root-owned package metadata")
    for name in ("/dev", "/dev/pts", "/proc", "/sys", "/run", "/tmp",
                 "/root", "/var/log", "/var/run"):
        guest_path(root, name).mkdir(parents=True, exist_ok=True)
    guest_path(root, "/tmp").chmod(0o1777)
    guest_path(root, "/root").chmod(0o700)
    repositories = guest_path(root, "/etc/apk/repositories")
    repositories.parent.mkdir(parents=True, exist_ok=True)
    repositories.write_bytes(Path("rootfs/alpine/m191-repositories").read_bytes())
    repositories.chmod(0o644)
    hostname = guest_path(root, "/etc/hostname")
    if not hostname.is_file():
        hostname.write_text("naseed\n")
        hostname.chmod(0o644)
    for name in ("/sbin/init", "/bin/sh"):
        # Resolve the parent but not the leaf: the original leaf may be a
        # guest-absolute symlink that should remain unmodified.
        parent_name, basename = posixpath.split(name)
        parent = guest_path(root, parent_name)
        entry = parent / basename
        if not entry.exists() and not entry.is_symlink():
            entry.symlink_to("/bin/busybox")
    configure_serial_inittab(root)
    print("[M191 ROOT CONFIG PASS] guest-safe /etc, /dev, /proc, /sys, "
          "OpenRC/BusyBox PID1 applet links")


def validate_tree(port: str, root: Path) -> dict[str, object]:
    root = root.resolve(strict=True)
    if not root.is_dir() or root.is_symlink():
        fail("rootfs tree missing or invalid")
    version = guest_path(root, "/etc/alpine-release").read_text().strip()
    if not version.startswith("3.23."):
        fail("wrong Alpine release: " + version)
    world = guest_path(root, "/etc/apk/world").read_text()
    for package in ("alpine-base", "openrc"):
        if package not in world.splitlines():
            fail("signed APK world is missing " + package)
    repositories = guest_path(root, "/etc/apk/repositories").read_text().splitlines()
    if repositories != [
        "https://dl-cdn.alpinelinux.org/alpine/v3.23/main",
        "https://dl-cdn.alpinelinux.org/alpine/v3.23/community",
    ]:
        fail("APK repositories are not fixed to official Alpine v3.23")
    for name in CRITICAL:
        if not guest_path(root, name).is_file():
            fail("missing guest filesystem member: " + name)
    inittab = guest_path(root, "/etc/inittab").read_text().splitlines()
    if inittab.count(SERIAL_GETTY) != 1 or any(
            len(fields := line.split(":", 3)) == 4 and
            fields[0] in LEGACY_GETTYS and fields[2] == "respawn"
            for line in inittab):
        fail("Alpine init is not bound to one physical NaSeed serial console")
    if not (root / "etc/apk/keys").is_dir() or not list((root / "etc/apk/keys").glob("*.pub")):
        fail("signed APK trust keys missing")
    for name in ("/dev", "/dev/pts", "/proc", "/sys", "/run", "/tmp"):
        if not guest_path(root, name).is_dir():
            fail("missing mountpoint: " + name)

    binary_count = 0
    expected_machine = ARCH_MACHINE[port]
    for directory, dirs, files in os.walk(
            root, followlinks=False,
            onerror=lambda error: fail("unreadable rootfs member: " + str(error))):
        dirs.sort()
        files.sort()
        for file in files:
            path = Path(directory) / file
            if not path.is_file() or path.is_symlink():
                continue
            with path.open("rb") as handle:
                header = handle.read(20)
            if not header.startswith(b"\x7fELF"):
                continue
            if len(header) < 20 or header[4] != 2 or header[5] != 1:
                fail("non-LP64 or non-little-endian ELF: " + str(path))
            found_machine = struct.unpack_from("<H", header, 18)[0]
            if found_machine != expected_machine:
                fail(f"wrong-ISA ELF {path}: {found_machine}, expected {expected_machine}")
            binary_count += 1
    if binary_count < 3:
        fail("insufficient native executable ELF files")
    digest = hashlib.sha256()
    for name in READBACK:
        data = guest_path(root, name).read_bytes()
        digest.update(name.encode() + b"\x00")
        digest.update(len(data).to_bytes(8, "little") + data)
    print(f"[M191 ALPINE TREE PASS] {port} Alpine {version}, "
          f"{binary_count} real LP64 native ELF files, signed APK world, "
          f"root/init/OpenRC and fixed v3.23 repositories; "
          f"critical_sha256={digest.hexdigest()}")
    return {"version": version, "binary_count": binary_count}


def check_mbr(disk: Path) -> None:
    if disk.stat().st_size != DISK_SIZE:
        fail("wrong physical root disk size")
    with disk.open("rb") as handle:
        mbr = handle.read(SECTOR)
    expected = struct.pack("<II", PART_START, PART_SECTORS)
    if (mbr[510:512] != b"\x55\xaa" or mbr[446 + 4] != 0x83 or
            mbr[446 + 8:446 + 16] != expected or mbr[462:510] != bytes(48)):
        fail("invalid MBR root partition geometry")


def extract_partition(disk: Path, part: Path) -> None:
    with disk.open("rb") as source, part.open("wb") as destination:
        source.seek(PART_START * SECTOR)
        remaining = PART_SECTORS * SECTOR
        while remaining:
            chunk = source.read(min(remaining, 1024 * 1024))
            if not chunk:
                fail("short disk partition")
            destination.write(chunk)
            remaining -= len(chunk)


def debugfs_cat(partition: Path, name: str) -> bytes:
    # debugfs may return 0 on a missing member; exact bytes are mandatory.
    answer = subprocess.run(
        ["debugfs", "-R", "cat " + name, str(partition)],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return answer.stdout


def prepare(port: str, root: Path, disk: Path) -> None:
    validate_tree(port, root)
    disk.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="m191-root-build-", dir=disk.parent) as work:
        temporary = Path(work)
        partition = temporary / "root.ext4"
        with partition.open("wb") as output:
            output.truncate(PART_SECTORS * SECTOR)
        # Match the already-tested M179 lwext4 feature subset. -d imports
        # genuine file content, directory modes, native symlinks and UID/GID.
        mkfs_prefix = [] if os.geteuid() == 0 else ["sudo"]
        subprocess.run(mkfs_prefix + [
            "mke2fs", "-q", "-F", "-t", "ext4", "-b", "4096",
            "-O", "^metadata_csum,^64bit", "-d", str(root), str(partition),
        ], check=True)
        subprocess.run(["sh", "scripts/check-ext4-fsck-clean.sh", str(partition)],
                       check=True, stdout=subprocess.PIPE)
        candidate = temporary / "alpine-root.img"
        with candidate.open("wb") as output:
            output.truncate(DISK_SIZE)
        with candidate.open("r+b") as raw, partition.open("rb") as source:
            mbr = bytearray(SECTOR)
            mbr[446:462] = (bytes((0, 0, 2, 0, 0x83, 0xFE, 0xFF, 0xFF)) +
                            struct.pack("<II", PART_START, PART_SECTORS))
            mbr[510:512] = b"\x55\xaa"
            raw.write(mbr)
            raw.seek(PART_START * SECTOR)
            shutil.copyfileobj(source, raw, 1024 * 1024)
            raw.flush()
            os.fsync(raw.fileno())
        check_mbr(candidate)
        # The final image is published atomically only after all host checks.
        verify(port, root, candidate)
        os.replace(candidate, disk)
    print(f"[M191 ROOT DISK PREPARED] {port} 256MiB MBR, "
          "254MiB original Alpine ext4 root partition")


def verify(port: str, root: Path, disk: Path) -> None:
    validate_tree(port, root)
    check_mbr(disk)
    with tempfile.TemporaryDirectory(prefix="m191-disk-verify-") as work:
        partition = Path(work) / "root.ext4"
        extract_partition(disk, partition)
        subprocess.run(["sh", "scripts/check-ext4-fsck-clean.sh", str(partition)],
                       check=True, stdout=subprocess.PIPE)
        for name in READBACK:
            expected = guest_path(root, name).read_bytes()
            actual = debugfs_cat(partition, name)
            if actual != expected:
                offset = next((i for i, (a, b) in enumerate(zip(actual, expected))
                               if a != b), min(len(actual), len(expected)))
                fail(f"physical root disk {name} differs at byte {offset}")
    print(f"[M191 HOST DISK PASS] {port} physical Alpine ext4 content, "
          "immutable MBR geometry, package metadata and e2fsck clean")


def main() -> None:
    if len(sys.argv) not in (4, 5) or sys.argv[1] not in (
            "configure", "check-tree", "prepare", "verify") or sys.argv[2] not in ARCH_MACHINE:
        raise SystemExit("usage: m191-alpine-root-disk.py configure|check-tree PORT ROOT | "
                         "prepare|verify PORT ROOT DISK")
    operation, port = sys.argv[1:3]
    root = Path(sys.argv[3]).resolve()
    if operation == "configure":
        if len(sys.argv) != 4:
            raise SystemExit(2)
        configure_tree(root)
        return
    if operation == "check-tree":
        if len(sys.argv) != 4:
            raise SystemExit(2)
        validate_tree(port, root)
        return
    if len(sys.argv) != 5:
        raise SystemExit(2)
    disk = Path(sys.argv[4]).resolve()
    (prepare if operation == "prepare" else verify)(port, root, disk)


if __name__ == "__main__":
    main()
