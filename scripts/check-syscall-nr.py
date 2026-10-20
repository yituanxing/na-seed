#!/usr/bin/env python3
"""Validate pinned NaSeed x86-64 Linux syscall numbers against host Linux UAPI."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
ours_path = root / "kernel" / "nr.h"
host_candidates = (
    Path("/usr/include/x86_64-linux-gnu/asm/unistd_64.h"),
    Path("/usr/include/asm/unistd_64.h"),
)
pattern = re.compile(r"^#define\s+__NR_([a-z][a-z0-9_]*)\s+(\d+)\s*$", re.MULTILINE)


def definitions(path: Path) -> dict[str, int]:
    contents = path.read_text(encoding="utf-8")
    pairs = pattern.findall(contents)
    if len(pairs) != len(dict(pairs)):
        raise ValueError(f"{path}: duplicate __NR_ definition")
    return {name: int(number) for name, number in pairs}


def main() -> None:
    local = definitions(ours_path)
    # Linux v6.18 native x86-64 table: 'common' + '64' only.
    # Edit these expected values only when intentionally upgrading the tag.
    required = {
        "read": 0, "write": 1, "exit": 60, "arch_prctl": 158,
        "getdents64": 217, "openat": 257, "newfstatat": 262,
        "rseq": 334, "clone3": 435, "file_setattr": 469,
    }
    if len(local) != 383 or max(local.values()) != 469:
        raise ValueError("nr.h no longer matches pinned Linux v6.18 table size")
    if len(set(local.values())) != len(local):
        raise ValueError("duplicate native x86-64 syscall number")
    for name, number in required.items():
        if local.get(name) != number:
            raise ValueError(f"__NR_{name}: expected {number}, got {local.get(name)}")
    host_path = next((p for p in host_candidates if p.is_file()), None)
    if host_path is None:
        raise FileNotFoundError("Install Linux x86-64 UAPI headers (linux-libc-dev)")
    host = definitions(host_path)
    compared = 0
    for name, number in host.items():
        if name not in local:
            # Installed kernel headers may be newer than pinned Linux v6.18.
            if number <= 469:
                raise ValueError(f"Linux UAPI {name}={number} is missing from nr.h")
            continue
        if local[name] != number:
            raise ValueError(
                f"__NR_{name}: NaSeed={local[name]} Linux UAPI={number}"
            )
        compared += 1
    if compared < 300:
        raise ValueError(f"Too few shared Linux UAPI syscalls: {compared}")
    print(f"[NR] PASS: {len(local)} pinned Linux v6.18 x86-64 numbers; "
          f"{compared} checked against host Linux UAPI ({host_path})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"[NR] FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
