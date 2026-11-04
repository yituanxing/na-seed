#!/usr/bin/env python3
"""Independent host oracle for public APKs and cross-application ext4 data."""

import importlib.util
from pathlib import Path
import hashlib
import re
import sqlite3
import sys
import tempfile
import zlib


PACKAGES = ("bash", "lua5.4", "sqlite", "git")
BINARIES = ("/bin/bash", "/usr/bin/lua5.4", "/usr/bin/sqlite3", "/usr/bin/git")
WITNESS = b"M197_PUBLIC_ALPINE_SOFTWARE_2026\n"


def load(path: str, alias: str):
    target = Path(__file__).with_name(path)
    spec = importlib.util.spec_from_file_location(alias, target)
    if spec is None or spec.loader is None:
        raise RuntimeError("M197 host oracle missing " + path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(port: str, root: Path, disk: Path) -> None:
    m195 = load("m195-alpine-disk-check.py", "m197_m195")
    m195.verify(port, root, disk, stage="M197",
                witness_path="/root/m197-wan.txt", witness=WITNESS,
                packages=PACKAGES, executables=BINARIES)
    m191 = m195.load_m191()
    root = root.resolve(strict=True)
    if (root / "root" / "m197-app").exists():
        raise ValueError("[M197 HOST FAIL] first-boot data already in baseline")
    with tempfile.TemporaryDirectory(prefix="m197-host-app-") as workspace:
        partition = Path(workspace) / "root.ext4"
        m191.extract_partition(disk.resolve(strict=True), partition)
        # M195.verify already ran strict fsck on these exact persistent
        # bytes. This second independent view checks application content.
        def read(name: str) -> bytes:
            return m191.debugfs_cat(partition, "/root/m197-app/" + name)

        if read("records.txt") != b"alpha 11\nbeta 22\ngamma 33\n":
            raise ValueError("[M197 HOST FAIL] Bash producer data missing")
        if read("summary.txt") != b"total=66\n":
            raise ValueError("[M197 HOST FAIL] original Lua result missing")
        head = read(".git/HEAD").decode("ascii").strip()
        if not head.startswith("ref: refs/heads/") or ".." in head:
            raise ValueError("[M197 HOST FAIL] Git HEAD ref invalid")
        commit = read(".git/" + head.removeprefix("ref: ")).decode("ascii").strip()
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError("[M197 HOST FAIL] original Git commit object absent")
        compressed = read(".git/objects/" + commit[:2] + "/" + commit[2:])
        try:
            git_object = zlib.decompress(compressed)
        except zlib.error as exc:
            raise ValueError("[M197 HOST FAIL] Git object compression invalid") from exc
        if (hashlib.sha1(git_object).hexdigest() != commit or
                not git_object.startswith(b"commit ") or
                b"\n\nNaSeed M197 public app\n" not in git_object):
            raise ValueError("[M197 HOST FAIL] Git commit does not match object")
        database = Path(workspace) / "ledger.db"
        sqlite_bytes = read("ledger.db")
        if not sqlite_bytes.startswith(b"SQLite format 3\x00"):
            raise ValueError("[M197 HOST FAIL] physical SQLite file header invalid")
        database.write_bytes(sqlite_bytes)
        # Python is a HOST-ONLY independent SQLite oracle; it neither
        # runs in NaSeed nor creates a replacement userland ELF.
        with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as sql:
            rows = sql.execute("SELECT name, points FROM ledger ORDER BY name").fetchall()
            integrity = sql.execute("PRAGMA integrity_check").fetchone()
            if rows != [("alpha", 11), ("beta", 22), ("gamma", 33)] or \
               integrity != ("ok",):
                raise ValueError("[M197 HOST FAIL] guest SQLite transaction invalid")
    print(f"[M197 PUBLIC SOFTWARE HOST PASS] {port} real ext4 Bash/Lua bytes, "
          "SQLite table, Git HEAD object, APK world and clean disk")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m197-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
