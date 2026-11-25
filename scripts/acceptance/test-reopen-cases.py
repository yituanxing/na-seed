#!/usr/bin/env python3
"""Independent byte-locked original M204-M238 second-QEMU manifest regression.

Digests predate deduplication, calculated from the 17 individually authored
original TSV rows. Every profile's original guest argv must remain exact.
"""
import hashlib
import runpy
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = {
    "m204": "cfe687cfd9b1f4049af2c776e506f93f32c6790d6888a88fd4cc320248b124a8",
    "m205": "f1d20608829918e47991401e4fc78d8d9aea2d4ac76f5d739277bd2adf9d075a",
    "m206": "99873a46f23337916d4ee1384397ed087c11f6d8d092c364a456fc62d2277a8f",
    "m215": "dfdff06f8888abd927c0e17f4c4626b233c3fcde3b3f35ec64d66c540bdeb50c",
    "m218": "2cd8dca39f43dc85d68962740e59f6636983570a74f7981fad08b89c1d91bba2",
    "m219": "7649284a2ae7a4571a697d96429e7b5813c4f0566f8d54664a933b30c8319714",
    "m220": "8ce6f5cfe0ebe974726d16d6d0bbc5850a50e9d6f3010a3155f490336af5fdd2",
    "m221": "512708f2f4ea98ab2ee054085b7a74f004791b8a49fc82bf46144cc44aa06977",
    "m222": "585d43c61c39b92b577ed4d647a049dcdc62fca531a029e361068c6261630df0",
    "m223": "1dce410fe62866a0521194fe33ab364d7d5f9212e7df2ab02980a22d5fe242c1",
    "m224": "51f4487e980d38d844f9380e629e6d8993877fd550e084b3b89fe0dddf620e78",
    "m225": "73da2b5592fa7de8cfb15e66379502419043b8293f8a7c45044b405568fb96c8",
    "m226": "08e6075148e7d5c565cfb827365092c1569077709294c5352388c56a45c73619",
    "m235": "101648dce7cb953ad87fcdc904dbb1cda02903cf5d656b106ca22ba302021977",
    "m236": "68a15f914e6621f5b4de1879e216809539f09f26e4c34747367e30c33f4ca41a",
    "m237": "7f8064de4649aca8b6e73aca242272ca28eaceb0db740e1826f642988f15ea73",
    "m238": "1e3290bf0e8bb8293e7197150f06e2e15c7cda1b1e1c745270ffe13281db9b68",
}


def main():
    source = ROOT / "scripts/acceptance/reopen_cases.py"
    row_for = runpy.run_path(str(source))["row_for"]
    for profile, digest in ORIGINAL.items():
        row = row_for(profile)
        assert hashlib.sha256((row + "\n").encode()).hexdigest() == digest, profile
        fields = row.split("\t")
        assert len(fields) == 6 and fields[0] == "m197-batch"
        assert fields[1] == "cpio" and fields[-1] == "/bin/busybox"
        check = subprocess.run([sys.executable, str(source), profile],
                               text=True, capture_output=True, check=True)
        assert check.stdout == row + "\n", profile
    for bad in ("", "m197", "m198", "m203", "m207", "m999", "m215;echo PASS", "m0215"):
        check = subprocess.run([sys.executable, str(source), bad],
                               text=True, capture_output=True)
        assert check.returncode != 0 and check.stdout == "", bad
    owner = (ROOT / "scripts/probe-m197-alpine-software-cohort.sh").read_text()
    assert owner.count("scripts/acceptance/reopen_cases.py") == 2
    assert 'python3 scripts/acceptance/reopen_cases.py "$nas_acceptance_profile"' in owner
    assert 'BUSYBOX_PARITY_MANIFEST="$generated"' in owner
    assert "scripts/m204-alpine-replay-cases.tsv" in owner
    print("[REOPEN MANIFEST PASS] 17 byte-pinned original guest argv rows and invalid-profile gates")


if __name__ == "__main__":
    main()
