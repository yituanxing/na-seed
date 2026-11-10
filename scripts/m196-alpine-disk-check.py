#!/usr/bin/env python3
"""M196 host oracle reuses M195's actual GPT/ext4/APK/ELF validation."""

import importlib.util
from pathlib import Path
import sys


def load_m195():
    path = Path(__file__).with_name("m195-alpine-disk-check.py")
    spec = importlib.util.spec_from_file_location("naseed_m195_disk", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M195 independent signed Alpine APK disk oracle missing")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m196-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    load_m195().verify(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
        stage="M196", witness_path="/root/m196-wan.txt",
        witness=b"M196_PUBLIC_ALPINE_APK_2026\n",
        packages=("tree", "jq"),
        executables=("/usr/bin/tree", "/usr/bin/jq"))
