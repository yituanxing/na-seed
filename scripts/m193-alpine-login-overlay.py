#!/usr/bin/env python3
"""Configure ONLY a disposable copy of the verified Alpine M191 rootfs.

The upstream BusyBox getty, login, init and ash remain byte-for-byte original.
This test-only password is not installed in the verified baseline root disk.
"""

import hashlib
import os
from pathlib import Path
import subprocess
import sys


TEST_PASSWORD = "naseed-m193-only"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    if len(sys.argv) != 3 or os.geteuid() != 0:
        raise SystemExit("usage (as root): m193-alpine-login-overlay.py BASE_ROOT TEST_ROOT")
    original, staged = (Path(argument).resolve(strict=True) for argument in sys.argv[1:])
    if original == staged or staged.is_relative_to(original):
        raise SystemExit("[M193 AUTH FAIL] refusing to modify baseline root")

    # The guest's /bin/ash, /bin/login, /sbin/getty may be links to BusyBox.
    # Compare the actual upstream bytes, as well as preserving symlink entries
    # through the independent cp -a staging operation.
    import importlib.util
    spec = importlib.util.spec_from_file_location("m191_disk", "scripts/m191-alpine-root-disk.py")
    assert spec is not None and spec.loader is not None
    disk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(disk)
    upstream = ("/sbin/init", "/sbin/getty", "/bin/login", "/bin/ash", "/bin/sh", "/bin/busybox")
    for guest in upstream:
        baseline = disk.guest_path(original, guest)
        copied = disk.guest_path(staged, guest)
        if not baseline.is_file() or not copied.is_file() or sha(baseline) != sha(copied):
            raise SystemExit("[M193 AUTH FAIL] official ELF changed: " + guest)

    securetty = disk.guest_path(staged, "/etc/securetty")
    if securetty.exists():
        entries = securetty.read_text().splitlines()
    else:
        entries = []
    if "ttyS0" not in entries:
        entries.append("ttyS0")
        securetty.write_text("\n".join(entries) + "\n")
    securetty.chmod(0o644)

    shadow = disk.guest_path(staged, "/etc/shadow")
    if not shadow.is_file():
        raise SystemExit("[M193 AUTH FAIL] official Alpine shadow missing")
    rows = shadow.read_text().splitlines()
    root_rows = [n for n, row in enumerate(rows) if row.split(":", 1)[0] == "root"]
    if len(root_rows) != 1 or len(rows[root_rows[0]].split(":")) != 9:
        raise SystemExit("[M193 AUTH FAIL] unexpected original shadow format")
    # Real password authentication, not autologin or a hardcoded kernel bypass.
    # OpenSSL creates a normal SHA-512 crypt hash for original Alpine login(1).
    crypt = subprocess.run(
        ["openssl", "passwd", "-6", "-salt", "NaSeedM193", TEST_PASSWORD],
        capture_output=True, text=True, check=True).stdout.strip()
    if not crypt.startswith("$6$NaSeedM193$"):
        raise SystemExit("[M193 AUTH FAIL] cannot create crypt password")
    fields = rows[root_rows[0]].split(":")
    fields[1] = crypt
    rows[root_rows[0]] = ":".join(fields)
    shadow.write_text("\n".join(rows) + "\n")
    shadow.chmod(0o600)
    print("[M193 AUTH OVERLAY] disposable copy trusts ttyS0 and has a test-only "
          "SHA-512 root password; all six original Alpine applet/ELF paths unchanged")


if __name__ == "__main__":
    main()
