#!/usr/bin/env python3
"""Host-only M204 contract: strict accounting, no fake skip/pass, M197 reused."""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
script = ROOT / "scripts/m204-compat-report.py"
spec = importlib.util.spec_from_file_location("m204_report", script)
assert spec is not None and spec.loader is not None
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

cases = report.manifest_cases(ROOT / "scripts/m204-alpine-cohort.tsv")
assert len(cases) == 12 and len({name for _, name in cases}) == 12
assert not ({"tree", "jq", "python3", "git", "bash", "lua5.4", "sqlite"} &
            {name for _, name in cases})


def first_uart(override=None):
    rows = ["M197_APP_WORKLOAD_PASS"]
    for category, package in cases:
        install, work = ("PASS", "PASS")
        if override and package in override:
            install, work = override[package]
        rows += [f"M204_RESULT\t{category}\t{package}\tinstall\t{install}",
                 f"M204_RESULT\t{category}\t{package}\tfunctional\t{work}"]
    return "\r\n".join(rows + ["M204_FIRST_DISCOVERY_COMPLETE"]) + "\r\n"


def reopen_uart(override=None):
    rows = ["M197_REOPEN_WORKLOAD_PASS"]
    for category, package in cases:
        status = override.get(package, "PASS") if override else "PASS"
        rows.append(f"M204_RESULT\t{category}\t{package}\treopen\t{status}")
    return "\r\n".join(rows + ["M204_REOPEN_DISCOVERY_COMPLETE"]) + "\r\n"


healthy = report.build_report(cases, first_uart(), reopen_uart())
assert healthy["total"] == healthy["newly_installed_full_pass"] == 12
assert not healthy["failures"] and healthy["preexisting"] == 0

broken = report.build_report(
    cases,
    first_uart({"redis": ("FAIL", "BLOCKED"),
                "make": ("PASS", "FAIL"),
                "tmux": ("PREEXISTING", "PASS")}) +
    "M204_ERROR\tredis\tinstall\tAPK timeout\r\n" +
    "[MISS] syscall nr=435\r\n",
    reopen_uart({"redis": "BLOCKED", "make": "FAIL"}),
)
assert broken["newly_installed_full_pass"] == 9
assert broken["preexisting"] == 1
assert set(broken["failures"]) == {"redis", "make", "tmux"}
assert broken["unknown_raw_syscalls_observed"]["first"] == {"435": 1}
assert next(x for x in broken["software"] if x["package"] == "redis")[
    "diagnostics"]["install"] == ["APK timeout"]

for invalid_first, invalid_reopen in (
    (first_uart().replace("M204_FIRST_DISCOVERY_COMPLETE", ""), reopen_uart()),
    (first_uart() + first_uart().splitlines()[1] + "\n", reopen_uart()),
    (first_uart(), reopen_uart().replace("M204_REOPEN_DISCOVERY_COMPLETE", "")),
    (first_uart({"redis": ("FAIL", "PASS")}), reopen_uart()),
):
    try:
        report.build_report(cases, invalid_first, invalid_reopen)
    except ValueError:
        pass
    else:
        raise AssertionError("forged/missing/inconsistent cohort evidence accepted")

old_probe = (ROOT / "scripts/probe-m197-alpine-software-cohort.sh").read_text()
new_probe = (ROOT / "scripts/probe-m204-alpine-compat-cohort.sh").read_text()
assert "NAS_M204_COMPAT_COHORT:-0" in old_probe
assert "scripts/m204-alpine-uart.py" in old_probe
assert "scripts/m204-alpine-reopen-cases.tsv" in old_probe
assert "m197-alpine-disk-check.py" in old_probe
assert "m196-public-wire-check.py" in old_probe
assert "NAS_M198_INTEGRATED_DESKTOP=0" in new_probe
assert "NAS_M199_ORIGINAL_OPENRC_DESKTOP=0" in new_probe
assert "--allow-untrusted" not in new_probe

print("[M204 HOST CONTRACT PASS] 12 distinct new samples, honest first/reopen "
      "accounting, no desktop, original M197 trust+network+ext4 chain reused")
