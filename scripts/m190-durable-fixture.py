#!/usr/bin/env python3
"""Inspect actual raw QEMU MBR ext4 bytes independently after EACH boot."""
from pathlib import Path
import runpy
import sys
import tempfile

EXPECTED = b"m190vimoriginal\nm190nanopersist\n"
PATH = "/m190-editor.txt"

def verify(disk: Path) -> None:
    src = Path(__file__).parent
    m180 = runpy.run_path(str(src / "m180-ext4-fixture.py"))
    m179 = runpy.run_path(str(src / "m179-ext4-fixture.py"))
    m180["verify"](disk)  # 3 old exact payloads + real e2fsck must stay clean
    with tempfile.TemporaryDirectory(prefix="m190-raw-ext4-") as tmp:
        part = Path(tmp) / "partition.ext4"
        m179["extract_partition"](disk, part)
        data = m179["debugfs_cat"](part, PATH)
        if data != EXPECTED:
            diff = next((i for i,(a,b) in enumerate(zip(data,EXPECTED))
                        if a!=b), min(len(data),len(EXPECTED)))
            raise ValueError(f"M190 durable file changed at {diff}; "
                             f"got {len(data)}, expected {len(EXPECTED)}")
    print("[M190 HOST EXT4 EDITOR PASS] real disk bytes 32/32, "
          "historic M179/M180 payloads and e2fsck clean")

if __name__=="__main__":
    if len(sys.argv)!=3 or sys.argv[1]!="verify":
        raise SystemExit("usage: m190-durable-fixture.py verify RAW_DISK")
    verify(Path(sys.argv[2]).resolve())
