#!/usr/bin/env python3
"""Materialize the canonical M197 ext4 second-boot workload, without TSV copies.

Only host-side one-row argv data is generated; genuine independent second
QEMU and guest program assertions remain owned by probe-m197-alpine-software-cohort.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
PILOT = frozenset((218, 219, 220, 221, 222, 223, 224))
COMPAT = frozenset((206, 235, 236, 237, 238))
BOOT_PHASE = frozenset((225, 226))
SUPPORTED = frozenset((204, 205, 215)) | PILOT | COMPAT | BOOT_PHASE


def row_for(profile: str) -> str:
    if not re.fullmatch(r"m[0-9]{3}", profile):
        raise ValueError("invalid reopen profile")
    number = int(profile[1:])
    if number not in SUPPORTED:
        raise ValueError("unsupported second-boot profile")
    base = ROOT / "scripts/m197-alpine-reopen-cases.tsv"
    original = [x for x in base.read_text().splitlines()
                if x and not x.startswith("#")]
    if len(original) != 1 or original[0].count("\t") != 5:
        raise ValueError("invalid canonical first-boot persistence contract")
    prefix, mode, command, marker, options, elf = original[0].split("\t")
    expected_prefix = ('sh -c "/sbin/apk info -e bash && /sbin/apk info -e lua5.4 '
                       '&& /sbin/apk info -e sqlite && /sbin/apk info -e git '
                       '&& /bin/bash /root/m197-software-workload.sh --reopen')
    suffix = ' && cat /root/m197-wan.txt"'
    if ((prefix, mode, marker, options, elf) !=
        ('m197-batch', 'cpio', 'M197_PUBLIC_ALPINE_SOFTWARE_2026',
         '-', '/bin/busybox') or command != expected_prefix + suffix):
        raise ValueError("canonical M197 guest contract has changed")
    if number in BOOT_PHASE:
        extra = (f"NAS_M{number}_BOOT_PHASE=reopen "
                 f"sh /root/m{number}-software-workload.sh --reopen")
    elif number in COMPAT:
        extra = (f"NAS_M{number}_COMPAT_COHORT=1 "
                 "sh /root/m205-software-workload.sh --reopen")
    elif number in PILOT or number == 215:
        extra = (f"NAS_M{number}_PILOT=1 "
                 "sh /root/m205-software-workload.sh --reopen")
    elif number == 205:
        extra = "sh /root/m205-software-workload.sh --reopen"
    else:
        extra = "sh /root/m204-software-workload.sh --reopen"
    return "\t".join((prefix, mode, command[:-len(suffix)] + " && " + extra + suffix,
                      marker, options, elf))


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: reopen_cases.py mNNN")
        print(row_for(sys.argv[1]))
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
