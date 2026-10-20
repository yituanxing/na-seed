#!/usr/bin/env python3
"""Cross-check NaSeed's Linux v6.18 errno values against Linux host UAPI."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
HOST = (
    Path("/usr/include/asm-generic/errno-base.h"),
    Path("/usr/include/asm-generic/errno.h"),
)
DEFINE = re.compile(r"^#define\s+(E[A-Z0-9_]+)\s+(\d+|E[A-Z0-9_]+)(?:\s|$)", re.M)


def read_defs(paths):
    result = {}
    for path in paths:
        for name, val in DEFINE.findall(path.read_text(encoding="utf-8")):
            if name in result:
                raise ValueError(f"{path}: duplicate {name}")
            result[name] = val
    return result


def main():
    ours = read_defs((ROOT / "kernel" / "errno.h",))
    if len(ours) != 133:
        raise ValueError(f"Linux v6.18 errno header count mismatch: {len(ours)}")
    for name, expected in {
        "ENOSYS": "38", "EFAULT": "14", "EINVAL": "22",
        "ENOENT": "2", "EOPNOTSUPP": "95", "EROFS": "30",
        "EWOULDBLOCK": "EAGAIN",
    }.items():
        if ours.get(name) != expected:
            raise ValueError(f"{name}: expected {expected}, found {ours.get(name)}")
    if not all(p.is_file() for p in HOST):
        raise FileNotFoundError("Install Linux UAPI headers (asm-generic/errno*.h)")
    host = read_defs(HOST)
    for name, expected in ours.items():
        if name not in host:
            raise ValueError(f"Host UAPI missing {name}")
        if host[name] != expected:
            raise ValueError(f"{name}: NaSeed={expected}, Linux UAPI={host[name]}")
    print(f"[ERRNO] PASS: {len(ours)} Linux v6.18 errno definitions match host UAPI")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as err:
        print(f"[ERRNO] FAIL: {err}", file=sys.stderr)
        sys.exit(1)
