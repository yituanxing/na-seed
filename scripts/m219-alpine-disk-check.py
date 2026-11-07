#!/usr/bin/env python3
"""Original utmpdump ELF/APK and durable ext4 via M206/M197 oracle."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys

source = Path(__file__).with_name("m206-alpine-disk-check.py")
spec = spec_from_file_location("m219_m206_disk", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M219 requires the existing M206 physical-disk oracle")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: m219-alpine-disk-check.py PORT SIGNED_ROOT DISK")
    root = Path(__file__).resolve().parent.parent
    owner.verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
                 candidates=root / "scripts/m219-candidate-elfs.tsv",
                 packages=root / "rootfs/alpine/packages-m219-candidates.txt",
                 expected_count=1, label="M219")
