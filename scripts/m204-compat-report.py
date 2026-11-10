#!/usr/bin/env python3
"""Fail-closed host accounting of two real M204 Alpine QEMU runs.

The report can succeed in discovery mode with application failures, but every
failed, blocked, preexisting or missing phase is explicitly accounted for.
Only --require-all enforces 12 wholly new three-phase successes.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


PREFIX = "M204_RESULT\t"
RAW_MISS = re.compile(r"\[MISS\] syscall nr=([0-9]+)")


def manifest_cases(path: Path) -> list[tuple[str, str]]:
    cases = []
    seen = set()
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 3:
            raise ValueError(f"manifest line {number} invalid: {raw!r}")
        category, package, workload = fields
        if package != workload and (package, workload) != ("procps-ng", "procps"):
            raise ValueError(f"manifest line {number} invalid workload mapping")
        if (not re.fullmatch(r"[a-z0-9-]+", category) or
                not re.fullmatch(r"[a-z0-9-]+", package) or
                package in seen):
            raise ValueError(f"manifest line {number} invalid or duplicated")
        seen.add(package)
        cases.append((category, package))
    if len(cases) != 12:
        raise ValueError(f"M204 requires exactly 12 sampled packages, got {len(cases)}")
    return cases


def parse_uart(text: str, stage: str, *, replay: bool = False) -> dict[tuple[str, str, str], str]:
    if stage not in ("first", "reopen"):
        raise ValueError("invalid UART stage")
    marker = ("M204_FIRST_DISCOVERY_COMPLETE" if stage == "first" else
              "M204_REPLAY_DISCOVERY_COMPLETE" if replay else
              "M204_REOPEN_DISCOVERY_COMPLETE")
    if marker not in (line.rstrip("\r") for line in text.splitlines()):
        raise ValueError(f"{stage} QEMU missing real guest completion marker")
    records = {}
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line.startswith(PREFIX):
            continue
        fields = line.split("\t")
        if len(fields) != 5 or fields[0] != "M204_RESULT":
            raise ValueError(f"malformed M204 UART result: {line!r}")
        _, category, package, operation, status = fields
        if operation not in (("install", "functional") if stage == "first"
                             else ("reopen",)):
            raise ValueError(f"unexpected {stage} operation: {operation}")
        if status not in ("PASS", "FAIL", "BLOCKED", "PREEXISTING"):
            raise ValueError(f"unexpected result status: {status}")
        key = category, package, operation
        if key in records:
            raise ValueError(f"duplicate M204 UART record: {key}")
        records[key] = status
    return records


def parse_error_tails(text: str, packages: set[str],
                      operations: set[str]) -> dict[tuple[str, str], list[str]]:
    tails = {}
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line.startswith("M204_ERROR\t"):
            continue
        fields = line.split("\t", 3)
        if len(fields) != 4:
            raise ValueError("malformed per-package error detail")
        _, package, operation, message = fields
        if package not in packages or operation not in operations:
            raise ValueError("unrecognized per-package error detail")
        if len(message) > 180:
            raise ValueError("oversized per-package error detail")
        item = tails.setdefault((package, operation), [])
        if len(item) >= 4:
            raise ValueError("excess per-package error detail")
        item.append(message)
    return tails


def build_report(cases: list[tuple[str, str]], first: str,
                 reopen: str, *, replay: bool = False) -> dict:
    first_records = parse_uart(first, "first")
    reopen_records = parse_uart(reopen, "reopen", replay=replay)
    expected_first = {(c, p, op) for c, p in cases
                      for op in ("install", "functional")}
    expected_reopen = {(c, p, "reopen") for c, p in cases}
    if set(first_records) != expected_first:
        raise ValueError("first QEMU missing/extra records: " +
                         repr(sorted(expected_first ^ set(first_records))))
    if set(reopen_records) != expected_reopen:
        raise ValueError("second QEMU missing/extra records: " +
                         repr(sorted(expected_reopen ^ set(reopen_records))))
    packages = {package for _, package in cases}
    first_errors = parse_error_tails(first, packages, {"install", "functional"})
    reopen_errors = parse_error_tails(reopen, packages, {"reopen"})
    software = []
    for category, package in cases:
        install = first_records[category, package, "install"]
        functional = first_records[category, package, "functional"]
        restart = reopen_records[category, package, "reopen"]
        if install not in ("PASS", "FAIL", "PREEXISTING"):
            raise ValueError(f"invalid install status for {package}: {install}")
        if functional not in ("PASS", "FAIL", "BLOCKED"):
            raise ValueError(f"invalid functional status for {package}")
        if restart not in ("PASS", "FAIL", "BLOCKED"):
            raise ValueError(f"invalid reopen status for {package}")
        if (install == "FAIL") != (functional == "BLOCKED"):
            raise ValueError(f"inconsistent first-boot installation for {package}")
        if (install == "FAIL") != (restart == "BLOCKED"):
            raise ValueError(f"inconsistent second-boot installation for {package}")
        new_pass = (install == functional == restart == "PASS")
        software.append(dict(category=category, package=package, install=install,
                             functional=functional, reopen=restart,
                             diagnostics={
                                 "install": first_errors.get((package, "install"), []),
                                 "functional": first_errors.get((package, "functional"), []),
                                 "reopen": reopen_errors.get((package, "reopen"), []),
                             },
                             newly_installed_full_pass=new_pass))
    missed = {}
    for stage, text in (("first", first), ("reopen", reopen)):
        missed[stage] = dict(sorted(Counter(RAW_MISS.findall(text)).items(),
                                    key=lambda pair: int(pair[0])))
    return dict(milestone="M204", acceptance=(
                    "verified-prior-disk-replay-not-current-cold-install" if replay else
                    "discovery-not-full-compatibility"),
                replay=replay,
                total=len(software), newly_installed_full_pass=sum(
                    item["newly_installed_full_pass"] for item in software),
                preexisting=sum(item["install"] == "PREEXISTING" for item in software),
                failures=[item["package"] for item in software
                          if not item["newly_installed_full_pass"]],
                unknown_raw_syscalls_observed=missed, software=software)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("first_uart", type=Path)
    parser.add_argument("reopen_uart", type=Path)
    parser.add_argument("json_output", type=Path)
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--replay", action="store_true",
                        help="third QEMU on a verified prior cold-boot disk, not fresh installation")
    args = parser.parse_args()
    try:
        report = build_report(manifest_cases(args.manifest),
                              args.first_uart.read_text(errors="replace"),
                              args.reopen_uart.read_text(errors="replace"),
                              replay=args.replay)
    except (OSError, ValueError) as exc:
        parser.error(f"[M204 INVALID EVIDENCE] {exc}")
    args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if args.replay:
        print(f"[M204 VERIFIED REPLAY REPORT] {report['newly_installed_full_pass']}/"
              f"{report['total']} previously signed-and-installed packages "
              "passed the current kernel's fresh third QEMU; "
              "NOT a current-commit fresh public install")
    else:
        print(f"[M204 DISCOVERY REPORT] {report['newly_installed_full_pass']}/"
              f"{report['total']} genuinely new packages passed install, functional, "
              f"second QEMU; preexisting={report['preexisting']}")
    for item in report["software"]:
        print(f"[M204 PACKAGE] {item['package']} install={item['install']} "
              f"functional={item['functional']} reopen={item['reopen']}")
    print(f"[M204 REPORT JSON] {args.json_output}")
    if args.require_all and report["newly_installed_full_pass"] != report["total"]:
        print("[M204 STRICT FAIL] discovery failures cannot count as full PASS")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
