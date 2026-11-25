#!/usr/bin/env python3
"""Derive exact per-ELF markers from their canonical signed ELF inventory.

Only profiles with previously byte-equivalent marker lists use derivation.
Special aliases, additional semantic markers and extended profiles continue
using their independently authored, fail-closed expected marker manifests.
Never inspect guest serial output to construct the expected list.
"""
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[2]
DERIVED = frozenset({
    "m112", "m113", "m114", "m115", "m116", "m119", "m120", "m121",
    "m122", "m124", "m125", "m126", "m127", "m128", "m129", "m130",
    "m131", "m132", "m133", "m135", "m136", "m139",
})


def expected_markers(profile: str, repo: Path = REPO) -> list[str]:
    if not re.fullmatch(r"m[0-9]{2,3}", profile):
        raise ValueError("invalid signed-ELF profile")
    if profile not in DERIVED:
        path = repo / "scripts" / f"{profile}-expected-markers.txt"
        markers = [line.strip() for line in path.read_text().splitlines()
                   if line.strip() and not line.startswith("#")]
    else:
        manifest = repo / "scripts" / f"{profile}-original-elfs.txt"
        binaries = [line.strip() for line in manifest.read_text().splitlines()
                    if line.strip() and not line.startswith("#")]
        n = int(profile[1:])
        expected_count = 20 if n in (136, 139) else 15
        if len(binaries) != expected_count or len(set(binaries)) != len(binaries):
            raise ValueError(f"{profile}: invalid independent ELF inventory count")
        if any(not re.fullmatch(r"(?:usr/)?(?:s?bin|libexec)/[A-Za-z0-9_.-]+", b)
               for b in binaries):
            raise ValueError(f"{profile}: invalid original ELF pathname")
        markers = [f"{profile.upper()}_{Path(b).name.upper().replace('-', '_').replace('.', '_')}_PASS"
                   for b in binaries]
        markers.append(f"{profile.upper()}_BATCH_PASS")
    if len(set(markers)) != len(markers) or not markers or \
            any(not re.fullmatch(profile.upper() + r"_[A-Z0-9_]+_PASS", m)
                for m in markers) or markers.count(profile.upper() + "_BATCH_PASS") != 1:
        raise ValueError(f"{profile}: invalid expected marker identity")
    return markers


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: profile_markers.py mNNN")
    try:
        print("\n".join(expected_markers(sys.argv[1])))
    except (ValueError, OSError) as exc:
        sys.exit(str(exc))
