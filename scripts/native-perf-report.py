#!/usr/bin/env python3
"""Compare verified real-guest source-build phases from QEMU serial logs.

This tool never creates acceptance PASS markers. It requires the original
application's final marker and the kernel's successful userspace exit.
Inputs should be repeated runs of the *same* ISA/QEMU/image; the caller must
record the exact commits and toolchain versions separately.
"""
import argparse
from pathlib import Path
import re
import statistics
import sys


PHASES = {
    "m38": ("gcc_hello", "gcc_readv", "gcc_fs_mode"),
    "m55": ("cmake_configure", "ninja_build", "ctest_first",
            "ninja_noop", "ninja_rebuild", "ctest_second"),
    "m56": ("source_copy", "kconfig", "kbuild"),
}
ACCEPTANCE = {
    "m38": ("M38_GCC_PASS", "M38_READV_PASS", "M38_FS_MODE_PASS"),
    "m55": ("M55_CMAKE_NINJA_FULL_SUITE_PASS",),
    "m56": ("M56_REAL_GUEST_BUSYBOX_FULL_PASS",),
}
MARKER = re.compile(
    r"^NAS_PERF_PHASE workload=(m38|m55|m56) "
    r"phase=([a-z0-9_]+) elapsed_cs=([0-9]+)$"
)


def parse_log(path: Path, workload: str) -> dict[str, int]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for required in (*ACCEPTANCE[workload], "[PASS] cmdline userspace exit 0"):
        if required not in lines:
            raise ValueError(f"{path}: missing genuine acceptance marker {required!r}")
    if any("[FAIL]" in line for line in lines):
        raise ValueError(f"{path}: genuine kernel/application failure marker present")
    phases: dict[str, int] = {}
    seen: list[str] = []
    for line in lines:
        if not line.startswith("NAS_PERF_PHASE "):
            continue
        match = MARKER.fullmatch(line)
        if not match:
            raise ValueError(f"{path}: malformed performance marker {line!r}")
        observed_workload, name, elapsed = match.groups()
        if observed_workload != workload:
            continue
        if name not in PHASES[workload] or name in phases:
            raise ValueError(f"{path}: unknown or duplicate phase {name!r}")
        phases[name] = int(elapsed)
        seen.append(name)
    missing = set(PHASES[workload]) - phases.keys()
    if missing:
        raise ValueError(f"{path}: missing performance phases {sorted(missing)}")
    if tuple(seen) != PHASES[workload]:
        raise ValueError(f"{path}: performance phase order is inconsistent")
    return phases


def render(workload: str, port: str, baseline: list[dict[str, int]],
           candidate: list[dict[str, int]], baseline_ref: str,
           candidate_ref: str) -> str:
    result = [
        f"Real guest {workload} / {port}: {baseline_ref} vs {candidate_ref}",
        f"Samples: baseline={len(baseline)}, candidate={len(candidate)}",
        "Phase                 baseline(s) candidate(s) speedup",
    ]
    for phase in PHASES[workload]:
        before = statistics.median(row[phase] for row in baseline) / 100
        after = statistics.median(row[phase] for row in candidate) / 100
        ratio = f"{before / after:.2f}x" if after > 0 else "n/a"
        result.append(f"{phase:<21} {before:>11.2f} {after:>12.2f} {ratio:>7}")
    if len(baseline) < 2 or len(candidate) < 2:
        result.append("EXPLORATORY: repeat each A/B arm before claiming a speedup")
    result.append("Guest phase times exclude host provisioning/boot; keep QEMU "
                  "settings, archive, ISA and cache state identical.")
    return "\n".join(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", required=True, choices=PHASES)
    parser.add_argument("--port", required=True,
                        choices=("amd64", "riscv64", "arm64"))
    parser.add_argument("--baseline-ref", required=True)
    parser.add_argument("--candidate-ref", required=True)
    parser.add_argument("--baseline", required=True, type=Path, nargs="+")
    parser.add_argument("--candidate", required=True, type=Path, nargs="+")
    args = parser.parse_args()
    try:
        before = [parse_log(path, args.workload) for path in args.baseline]
        after = [parse_log(path, args.workload) for path in args.candidate]
    except (OSError, ValueError) as exc:
        print(f"[NATIVE PERF FAIL] {exc}", file=sys.stderr)
        return 1
    print(render(args.workload, args.port, before, after,
                 args.baseline_ref, args.candidate_ref))
    return 0


if __name__ == "__main__":
    sys.exit(main())
