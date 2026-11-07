#!/usr/bin/env python3
"""Reuse M206 physical-ext4 and signed target-ISA APK/ELF SHA oracle."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys

source = Path(__file__).with_name("m206-alpine-disk-check.py")
spec = spec_from_file_location("m220_m206_disk", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M220 requires M206 physical ext4 disk oracle")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m220-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    root = Path(__file__).resolve().parent.parent
    owner.verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
                 candidates=root / "scripts/m220-candidate-elfs.tsv",
                 packages=root / "rootfs/alpine/packages-m220-candidates.txt",
                 expected_count=1, label="M220")
