#!/usr/bin/env python3
"""Compare verified real-guest source-build phases and optional counters.

This tool never creates acceptance PASS markers. It requires the original
application's final marker and the kernel's successful userspace exit.
Without matched run manifests, numeric ratios are exploratory. Counters are
reported only when both arms actually measured the same metric.
"""
import argparse
import json
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
COUNTER = re.compile(
    r"^NAS_PERF_COUNTER workload=(m38|m55|m56) "
    r"scope=(cpu|syscall|vfs) metric=([a-z][a-z0-9_.-]{0,63}) "
    r"value=([0-9]+) unit=(ns|us|cs|count|bytes)$"
)
SCOPES = ("cpu", "syscall", "vfs")
COMPARABLE_FIELDS = (
    "workload", "port", "qemu_version", "qemu_machine", "qemu_args",
    "rootfs_sha256", "source_sha256", "toolchain", "build_flags",
    "cache_state", "guest_ram_mib", "vcpus", "instrumentation",
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


def parse_counters(path: Path, workload: str) -> dict[str, int]:
    """Read optional measured values; never synthesize absent counters."""
    counters: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("NAS_PERF_COUNTER "):
            continue
        match = COUNTER.fullmatch(line)
        if not match:
            raise ValueError(f"{path}: malformed counter {line!r}")
        observed, scope, metric, value, unit = match.groups()
        if observed != workload:
            continue
        key = f"{scope}.{metric}.{unit}"
        if key in counters:
            raise ValueError(f"{path}: duplicate counter {key!r}")
        counters[key] = int(value)
    return counters


def load_manifest(path: Path) -> dict:
    """Require exact, recorded inputs before treating two runs as comparable."""
    meta = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(meta, dict):
        raise ValueError(f"{path}: manifest must be an object")
    missing = [field for field in COMPARABLE_FIELDS if field not in meta]
    if missing:
        raise ValueError(f"{path}: incomplete manifest fields {missing}")
    if any(meta[field] is None or meta[field] == "" for field in COMPARABLE_FIELDS):
        raise ValueError(f"{path}: manifest has blank comparison fields")
    if meta["cache_state"] not in ("cold", "warm", "controlled"):
        raise ValueError(f"{path}: unknown cache_state")
    if type(meta["guest_ram_mib"]) is not int or meta["guest_ram_mib"] <= 0:
        raise ValueError(f"{path}: guest_ram_mib must be positive integer")
    if type(meta["vcpus"]) is not int or meta["vcpus"] <= 0:
        raise ValueError(f"{path}: vcpus must be positive integer")
    for field in ("rootfs_sha256", "source_sha256"):
        if not isinstance(meta[field], str) or not re.fullmatch(
                r"[a-fA-F0-9]{64}", meta[field]):
            raise ValueError(f"{path}: {field} must be SHA256 hex")
    return meta


def check_manifests(baseline_paths: list[Path], candidate_paths: list[Path],
                    workload: str, port: str) -> dict:
    all_paths = baseline_paths + candidate_paths
    if not all_paths:
        raise ValueError("no manifests")
    meta = [load_manifest(path) for path in all_paths]
    for path, row in zip(all_paths, meta):
        if row["workload"] != workload or row["port"] != port:
            raise ValueError(f"{path}: workload/ISA mismatch")
    expected = {k: meta[0][k] for k in COMPARABLE_FIELDS}
    for path, row in zip(all_paths[1:], meta[1:]):
        mismatched = [key for key in COMPARABLE_FIELDS if row[key] != expected[key]]
        if mismatched:
            raise ValueError(f"{path}: incomparable inputs {mismatched}")
    return expected


def counter_table(baseline: list[dict[str, int]],
                  candidate: list[dict[str, int]]) -> list[str]:
    samples = baseline + candidate
    keys = set(samples[0]) if samples else set()
    if any(set(sample) != keys for sample in samples):
        raise ValueError("counter availability differs between runs; do not cherry-pick")
    result = ["Measured counters (identical capture points required):"]
    for scope in SCOPES:
        scoped = sorted(key for key in keys if key.startswith(scope + "."))
        if not scoped:
            result.append(f"  {scope}: not-measured")
            continue
        for key in scoped:
            old = statistics.median(row[key] for row in baseline)
            new = statistics.median(row[key] for row in candidate)
            ratio = f"{old / new:.2f}x" if new else "n/a"
            result.append(f"  {key}: {old:g} -> {new:g} "
                          f"(baseline/candidate={ratio}; not necessarily speedup)")
    return result


def render(workload: str, port: str, baseline: list[dict[str, int]],
           candidate: list[dict[str, int]], baseline_ref: str,
           candidate_ref: str, *, counters_before=None, counters_after=None,
           comparable=False) -> str:
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
    if counters_before is not None and counters_after is not None:
        result.extend(counter_table(counters_before, counters_after))
    if not comparable:
        result.append("UNVERIFIED COMPARABILITY: no matched input manifests; "
                      "ratios are exploratory, not a measured optimization claim")
    else:
        result.append("Input manifests match across both arms; this alone does "
                      "not establish statistical confidence")
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
    parser.add_argument("--baseline-manifests", type=Path, nargs="+")
    parser.add_argument("--candidate-manifests", type=Path, nargs="+")
    parser.add_argument("--strict-metadata", action="store_true",
                        help="fail if a matched input manifest is missing")
    args = parser.parse_args()
    try:
        before = [parse_log(path, args.workload) for path in args.baseline]
        after = [parse_log(path, args.workload) for path in args.candidate]
        counters_before = [parse_counters(path, args.workload) for path in args.baseline]
        counters_after = [parse_counters(path, args.workload) for path in args.candidate]
        # Both arms must expose the same observables, including missing ones.
        counter_table(counters_before, counters_after)
        comparable = False
        if args.baseline_manifests or args.candidate_manifests:
            if not args.baseline_manifests or not args.candidate_manifests or \
               len(args.baseline_manifests) != len(args.baseline) or \
               len(args.candidate_manifests) != len(args.candidate):
                raise ValueError("provide one manifest per measured serial log")
            check_manifests(args.baseline_manifests, args.candidate_manifests,
                            args.workload, args.port)
            comparable = True
        elif args.strict_metadata:
            raise ValueError("strict metadata requires manifests for every run")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"[NATIVE PERF FAIL] {exc}", file=sys.stderr)
        return 1
    print(render(args.workload, args.port, before, after,
                 args.baseline_ref, args.candidate_ref,
                 counters_before=counters_before, counters_after=counters_after,
                 comparable=comparable))
    return 0


if __name__ == "__main__":
    sys.exit(main())
