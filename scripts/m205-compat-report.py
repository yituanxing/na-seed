#!/usr/bin/env python3
"""Fail-closed M205 original-Alpine per-entry QEMU evidence accounting.

This host parser does not create a QEMU pass. Its callers must separately prove
the official signed-APK provenance, actual native ELF/SHA uniqueness, two
independent physical QEMU boots, public-wire transaction and ext4/fsck. A
successful discovery report may contain failed programs; --require-all refuses
them. In particular, 20 executable *paths* contain only 19 distinct ELFs:
lnstat and rtstat share a binary and must never be counted twice as ELFs.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path, PurePosixPath
import re


RESULT = "M205_RESULT\t"
ERROR = "M205_ERROR\t"
HEADER = "package\telf_path\tfamily\tprobe"
TAG = re.compile(r"[a-z][a-z0-9-]*")
PACKAGE = re.compile(r"[a-z0-9][a-z0-9.+_-]*")
RAW_MISS = re.compile(r"\[MISS\] syscall nr=([0-9]+)")
PHASES = {"first": ("install", "functional"), "reopen": ("reopen",)}
MARKERS = {"first": "M205_FIRST_DISCOVERY_COMPLETE",
           "reopen": "M205_REOPEN_DISCOVERY_COMPLETE"}


def manifest_cases(path: Path) -> list[dict[str, str]]:
    lines = path.read_text().splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError("M205 manifest missing exact four-column header")
    cases: list[dict[str, str]] = []
    seen: set[str] = set()
    for line_number, line in enumerate(lines[1:], 2):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 4:
            raise ValueError(f"manifest line {line_number}: expected 4 TSV fields")
        package, elf_path, family, probe = parts
        path_bits = PurePosixPath(elf_path)
        if (not PACKAGE.fullmatch(package) or not TAG.fullmatch(family) or
                not TAG.fullmatch(probe) or path_bits.is_absolute() or
                ".." in path_bits.parts or path_bits.as_posix() != elf_path or
                not elf_path.startswith(("bin/", "sbin/", "usr/bin/", "usr/sbin/")) or
                any(c.isspace() for c in elf_path)):
            raise ValueError(f"manifest line {line_number}: invalid case")
        if elf_path in seen:
            raise ValueError(f"manifest line {line_number}: duplicate ELF path")
        seen.add(elf_path)
        cases.append(dict(package=package, elf_path=elf_path,
                          family=family, probe=probe))
    if len(cases) != 20:
        raise ValueError(f"M205 requires 20 individually accountable command paths, got {len(cases)}")
    return cases


def parse_uart(text: str, stage: str) -> dict[tuple[str, str, str], str]:
    if stage not in PHASES:
        raise ValueError("invalid QEMU stage")
    lines = [line.rstrip("\r") for line in text.splitlines()]
    if lines.count(MARKERS[stage]) != 1:
        raise ValueError(f"{stage} QEMU missing/duplicate guest completion marker")
    records: dict[tuple[str, str, str], str] = {}
    for line in lines:
        if not line.startswith(RESULT):
            continue
        fields = line.split("\t")
        if len(fields) != 5:
            raise ValueError(f"malformed M205 result: {line!r}")
        _, package, elf_path, phase, status = fields
        if phase not in PHASES[stage]:
            raise ValueError(f"unexpected {stage} phase: {phase}")
        if status not in ("PASS", "PREEXISTING", "FAIL", "BLOCKED"):
            raise ValueError(f"unsupported M205 status: {status}")
        key = package, elf_path, phase
        if key in records:
            raise ValueError(f"duplicate M205 result: {key}")
        records[key] = status
    return records


def parse_error_tails(text: str, expected: set[tuple[str, str]],
                      phases: tuple[str, ...]) -> dict[tuple[str, str, str], list[str]]:
    messages: dict[tuple[str, str, str], list[str]] = {}
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line.startswith(ERROR):
            continue
        fields = line.split("\t", 4)
        if len(fields) != 5:
            raise ValueError("malformed M205 error tail")
        _, package, elf_path, phase, detail = fields
        if (package, elf_path) not in expected or phase not in phases:
            raise ValueError("error tail belongs to unknown case/phase")
        if len(detail) > 180 or "\t" in detail:
            raise ValueError("invalid M205 error message length/format")
        key = package, elf_path, phase
        messages.setdefault(key, []).append(detail)
        if len(messages[key]) > 4:
            raise ValueError("too many M205 error lines")
    return messages


def build_report(cases: list[dict[str, str]], first: str, reopen: str) -> dict:
    first_records = parse_uart(first, "first")
    reopen_records = parse_uart(reopen, "reopen")
    expected = {(case["package"], case["elf_path"]) for case in cases}
    expected_first = {(pkg, path, phase) for pkg, path in expected
                      for phase in PHASES["first"]}
    expected_reopen = {(pkg, path, "reopen") for pkg, path in expected}
    if set(first_records) != expected_first:
        raise ValueError("first QEMU missing/extra records: " +
                         repr(sorted(expected_first ^ set(first_records))))
    if set(reopen_records) != expected_reopen:
        raise ValueError("second QEMU missing/extra records: " +
                         repr(sorted(expected_reopen ^ set(reopen_records))))
    errors = {}
    errors.update(parse_error_tails(first, expected, PHASES["first"]))
    errors.update(parse_error_tails(reopen, expected, PHASES["reopen"]))
    results = []
    for case in cases:
        pkg, path = case["package"], case["elf_path"]
        install = first_records[pkg, path, "install"]
        functional = first_records[pkg, path, "functional"]
        restart = reopen_records[pkg, path, "reopen"]
        if install not in ("PASS", "FAIL", "PREEXISTING"):
            raise ValueError(f"invalid install state: {pkg}/{path}")
        if functional not in ("PASS", "FAIL", "BLOCKED"):
            raise ValueError(f"invalid functional state: {pkg}/{path}")
        if restart not in ("PASS", "FAIL", "BLOCKED"):
            raise ValueError(f"invalid second-boot state: {pkg}/{path}")
        if (install == "FAIL") != (functional == "BLOCKED"):
            raise ValueError(f"contradictory installation and function: {pkg}/{path}")
        if (install == "FAIL") != (restart == "BLOCKED"):
            raise ValueError(f"contradictory installation and reopen: {pkg}/{path}")
        complete = install == functional == restart == "PASS"
        results.append({**case, "install": install, "functional": functional,
                        "reopen": restart, "newly_installed_full_pass": complete,
                        "diagnostics": {phase: errors.get((pkg, path, phase), [])
                                        for phase in ("install", "functional", "reopen")}})
    missed = {stage: dict(sorted(Counter(RAW_MISS.findall(log)).items(),
                                key=lambda item: int(item[0])))
              for stage, log in (("first", first), ("reopen", reopen))}
    return dict(milestone="M205", acceptance="discovery-not-full-compatibility",
                total_entrypoints=len(results),
                newly_installed_full_pass_entrypoints=sum(
                    item["newly_installed_full_pass"] for item in results),
                preexisting=sum(item["install"] == "PREEXISTING" for item in results),
                failures=[item["elf_path"] for item in results
                          if not item["newly_installed_full_pass"]],
                # 19 unique SHA were proven separately by the signed host
                # inventory, not by UART or this report; never infer them here.
                independent_elf_sha_attested_here=False,
                unknown_raw_syscalls_observed=missed, software=results)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("first_uart", type=Path)
    parser.add_argument("reopen_uart", type=Path)
    parser.add_argument("json_output", type=Path)
    parser.add_argument("--require-all", action="store_true")
    args = parser.parse_args()
    try:
        report = build_report(manifest_cases(args.manifest),
                              args.first_uart.read_text(errors="replace"),
                              args.reopen_uart.read_text(errors="replace"))
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M205 INVALID EVIDENCE] {exc}")
    print(f"[M205 DISCOVERY REPORT] "
          f"{report['newly_installed_full_pass_entrypoints']}/"
          f"{report['total_entrypoints']} original Alpine executable paths "
          "passed fresh install, real function, second QEMU; "
          f"preexisting={report['preexisting']} (unique ELF SHA is a separate host gate)")
    for item in report["software"]:
        print(f"[M205 ENTRY] {item['elf_path']} install={item['install']} "
              f"functional={item['functional']} reopen={item['reopen']}")
    print(f"[M205 REPORT JSON] {args.json_output}")
    if args.require_all and report["newly_installed_full_pass_entrypoints"] != 20:
        print("[M205 STRICT FAIL] discovery results cannot count as full compatibility")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
