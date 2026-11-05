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
assert healthy["replay"] is False

replayed = report.build_report(
    cases, first_uart(),
    reopen_uart().replace("M204_REOPEN_DISCOVERY_COMPLETE",
                          "M204_REPLAY_DISCOVERY_COMPLETE"), replay=True)
assert replayed["replay"] is True
assert replayed["newly_installed_full_pass"] == 12
assert replayed["acceptance"] == "verified-prior-disk-replay-not-current-cold-install"
try:
    report.build_report(cases, first_uart(), reopen_uart(), replay=True)
except ValueError:
    pass
else:
    raise AssertionError("replay accepted an old second-boot completion marker")

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

# M197 retains its established no-cache baseline; M204 must reuse the
# first public transaction's signed index, not run another WAN `apk update`.
uart_path = ROOT / "scripts/m204-alpine-uart.py"
uart_spec = importlib.util.spec_from_file_location("m204_cache_contract", uart_path)
assert uart_spec is not None and uart_spec.loader is not None
uart = importlib.util.module_from_spec(uart_spec)
uart_spec.loader.exec_module(uart)
assert uart.M197.COMMANDS[3][0].startswith("apk --no-cache add ")
assert uart.COMMANDS[3][0].startswith("apk add bash lua5.4 sqlite git;")
assert uart.M197.COMMANDS[3][1] == uart.COMMANDS[3][1]
guest_lines = (ROOT / "tests/m204-software-workload.sh").read_text().splitlines()
guest = "\n".join(guest_lines)
assert "apk update" not in "\n".join(
    line for line in guest_lines if not line.lstrip().startswith("#"))
# Each reboot owns its service processes. An inherited/detached nginx or
# Redis daemon could pin the root mount after PID1 exits and falsify durable
# ext4 re-open acceptance even when every program returned success.
assert "pgrep -f init" not in guest
assert "pgrep -f '^/bin/busybox sleep 20$'" in guest
assert "master_process off;" in guest
assert "--daemonize yes" not in guest
assert "--daemonize no" in guest
assert 'wait "$server_pid"' in guest

boot = (ROOT / "ports/lwext4/guest_boot.c").read_text()
vfs = (ROOT / "kernel/fs/vfs.c").read_text()
assert "[LWEXT4 SHUTDOWN ERROR]" in boot
assert "[M191 MOUNT BUSY]" in vfs

print("[M204 HOST CONTRACT PASS] 12 distinct new samples, honest first/reopen "
      "accounting, no desktop, original M197 trust+network+ext4 chain reused")
