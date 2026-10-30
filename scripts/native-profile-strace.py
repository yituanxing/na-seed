#!/usr/bin/env python3
"""Convert a genuine guest `LC_ALL=C strace -f -c` summary to counters.

strace perturbs timing and therefore its output belongs in a separate
instrumented diagnostic run, not an uninstrumented source-build A/B.
"""
import argparse
from decimal import Decimal, InvalidOperation
from pathlib import Path
import re
import sys


def parse_summary(path: Path) -> dict[str, tuple[int, int]]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not any("% time" in line and "syscall" in line for line in lines):
        raise ValueError("missing original strace syscall summary header")
    entries: dict[str, tuple[int, int]] = {}
    for line in lines:
        fields = line.split()
        if len(fields) < 5 or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", fields[0]):
            continue
        # %time seconds usecs/call calls [errors] syscall
        name = fields[-1]
        if not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_]+", name):
            continue
        if len(fields) not in (5, 6) or name in entries:
            raise ValueError(f"malformed/duplicate strace summary row {line!r}")
        try:
            seconds = Decimal(fields[1])
            elapsed_ns = seconds * Decimal(1000000000)
            calls = int(fields[3])
            if seconds < 0 or calls < 0 or elapsed_ns != elapsed_ns.to_integral_value():
                raise ValueError(f"invalid strace metric {line!r}")
            if len(fields) == 6 and int(fields[4]) > calls:
                raise ValueError(f"invalid strace errors {line!r}")
        except (InvalidOperation, ValueError) as exc:
            raise ValueError(f"bad strace summary row {line!r}") from exc
        entries[name] = (int(elapsed_ns), calls)
    if "total" not in entries or len(entries) < 2:
        raise ValueError("strace summary missing total or individual syscall rows")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", required=True, choices=("m38", "m55", "m56"))
    parser.add_argument("--phase", required=True)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--syscalls", default="",
                        help="comma-separated syscall names, default total only")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9_]+", args.phase):
        parser.error("phase must be lowercase snake_case")
    names = ["total"] + ([x for x in args.syscalls.split(",") if x]
                         if args.syscalls else [])
    if any(not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_]+", x) for x in names):
        parser.error("invalid syscall filter")
    try:
        entries = parse_summary(args.input)
        for name in names:
            if name not in entries:
                raise ValueError(f"requested syscall {name!r} was not measured")
        for name in dict.fromkeys(names):
            time_ns, calls = entries[name]
            metric = f"{args.phase}.{name}"
            print(f"NAS_PERF_COUNTER workload={args.workload} scope=syscall "
                  f"metric={metric}.time value={time_ns} unit=ns")
            print(f"NAS_PERF_COUNTER workload={args.workload} scope=syscall "
                  f"metric={metric}.calls value={calls} unit=count")
    except (OSError, ValueError) as exc:
        print(f"[NATIVE STRACE PROFILE FAIL] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
