#!/usr/bin/env python3
"""Host-only ext4 oracle for real QMP keyboard -> Alpine PTY -> Bash."""
from pathlib import Path
import importlib.util
import sys
import tempfile


WITNESS = b"m198vimrealinput\n"


def load(name, alias):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M198 missing existing independent host oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(port: str, root: Path, disk: Path):
    # Preserve the complete M197 native Bash/Lua/SQLite/Git/HTTPS proof.
    m197 = load("m197-alpine-disk-check.py", "m198_m197_disk")
    m197.verify(port, root, disk)
    m191 = load("m191-alpine-root-disk.py", "m198_m191_disk")
    if m191.guest_path(root.resolve(strict=True), "/root/m198-editor.txt").exists():
        raise ValueError("[M198 HOST FAIL] GUI result was in the signed root")
    with tempfile.TemporaryDirectory(prefix="m198-gui-ext4-") as tmp:
        partition = Path(tmp) / "root.ext4"
        m191.extract_partition(disk.resolve(strict=True), partition)
        if m191.debugfs_cat(partition, "/root/m198-editor.txt") != WITNESS:
            raise ValueError("[M198 HOST FAIL] physical original Vim bytes missing")
        installed = m191.debugfs_cat(partition, "/lib/apk/db/installed")
        world = m191.debugfs_cat(partition, "/etc/apk/world").splitlines()
        for pkg in (b"vim", b"nano", b"ncurses-terminfo-base"):
            if b"P:" + pkg + b"\n" not in b"\n" + installed or pkg not in world:
                raise ValueError("[M198 HOST FAIL] original signed GUI package "
                                 + pkg.decode() + " absent")
    print(f"[M198 PHYSICAL ALPINE DESKTOP HOST PASS] {port} original "
          "Alpine signed GUI packages, QMP original Vim, persisted physical "
          "ext4 bytes, M197 public app state and clean fsck")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m198-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
