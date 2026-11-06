#!/usr/bin/env python3
"""Host-only negative/positive contract for fail-closed M205 UART accounting.

Passing this test is not a guest execution or a signed-ELF acceptance gate.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MODULE = ROOT / "scripts/m205-compat-report.py"
CANDIDATES = ROOT / "scripts/m205-candidate-elfs.tsv"
COHORT = ROOT / "scripts/m205-alpine-cohort.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m205-candidates.txt"
GUEST = ROOT / "tests/m205-software-workload.sh"
M204_GUEST = ROOT / "tests/m204-software-workload.sh"
M204_COHORT = ROOT / "scripts/m204-alpine-cohort.tsv"
reporter = runpy.run_path(str(MODULE))
manifest_cases = reporter["manifest_cases"]
build_report = reporter["build_report"]
parse_uart = reporter["parse_uart"]
parse_error_tails = reporter["parse_error_tails"]


def raises(fragment: str, func, *args, **kwargs) -> None:
    try:
        func(*args, **kwargs)
    except ValueError as exc:
        assert fragment in str(exc), (fragment, str(exc))
    else:
        raise AssertionError(f"expected fail-closed ValueError containing {fragment!r}")


def uart(cases: list[dict[str, str]], phase: str,
         override: dict[tuple[str, str, str], str] | None = None) -> str:
    override = override or {}
    operations = ("install", "functional") if phase == "first" else ("reopen",)
    lines: list[str] = []
    for item in cases:
        pkg, elf = item["package"], item["elf_path"]
        for operation in operations:
            status = override.get((pkg, elf, operation), "PASS")
            lines.append(f"M205_RESULT\t{pkg}\t{elf}\t{operation}\t{status}")
    lines.append("M205_FIRST_DISCOVERY_COMPLETE" if phase == "first"
                 else "M205_REOPEN_DISCOVERY_COMPLETE")
    return "\n".join(lines) + "\n"


def check_guest_manifest_isolation() -> None:
    """Execute actual M204/M205 TSV-loop headers against stdin-stealing child.

    This reproduces the exact M205 three-ISA 19/20 error without a QEMU boot:
    the original `done < "$manifest"` bound every child's fd0 to the
    manifest, and a native utility could consume a later row. Both first-boot
    APK pre-scan and each first/reopen software loop must read only fd9.
    """
    contracts = ((GUEST, COHORT, 20,
                  ("pkg path family check", "package elf family case_id")),
                 (M204_GUEST, M204_COHORT, 12,
                  ("category package test_id",)))
    for source, cohort, expected, loops in contracts:
        guest = source.read_text()
        assert "exec 0</dev/null" in guest, source
        assert guest.count('done 9< "$manifest"') == len(loops), source
        for fields in loops:
            check_guest_manifest_loop(guest, source, cohort, expected, fields)


def check_guest_manifest_loop(guest: str, source: Path, cohort: Path,
                              expected: int, fields: str) -> None:
        name = fields.split()[0]
        header = (f'while IFS="$tab" read -r {fields} <&9 || '
                  f'[ -n "${name}" ]; do')
        # Avoid a false static-only test: execute these exact production
        # headers, including fd9 positioning, with an actual child that
        # attempts to drain its inherited stdin on EACH of twenty rows.
        assert len(re.findall(re.escape(header), guest)) == 1, (source, header)
        script = f'''set -eu
tab=$(printf '\\t')
manifest=$1
exec 0</dev/null
n=0
{header}
    case "${name}" in ''|'#'*|package) continue ;; esac
    sh -c 'while IFS= read -r stolen; do :; done'
    n=$((n+1))
done 9< "$manifest"
printf '%s\\n' "$n"
'''
        completed = subprocess.run(["sh", "-c", script, "m205-loop", str(cohort)],
                                   input=cohort.read_text(), capture_output=True,
                                   text=True, check=False, timeout=15)
        assert completed.returncode == 0, completed.stderr
        assert completed.stdout == f"{expected}\n", (source, header, completed.stdout)


def main() -> None:
    check_guest_manifest_isolation()
    cases = manifest_cases(CANDIDATES)
    # The signed-host census, guest installer and strict report must point to
    # EXACTLY the same twenty original ELF paths and sixteen package owners.
    assert manifest_cases(COHORT) == cases
    package_lines = [line.strip() for line in PACKAGES.read_text().splitlines()
                     if line.strip() and not line.lstrip().startswith("#")]
    assert len(package_lines) == len(set(package_lines))
    assert set(package_lines) == {item["package"] for item in cases}
    assert len(cases) == 20
    assert len({item["elf_path"] for item in cases}) == 20
    assert len({item["package"] for item in cases}) == 16
    assert [case["elf_path"] for case in cases if case["package"] == "bind-tools"] == [
        "usr/bin/named-compilezone", "usr/sbin/tsig-keygen"]
    # Two commands can be paths into the SAME package and need separate
    # installation / function / second-boot records.
    clean_first, clean_reopen = uart(cases, "first"), uart(cases, "reopen")
    result = build_report(cases, clean_first, clean_reopen)
    assert result["total_entrypoints"] == 20
    assert result["newly_installed_full_pass_entrypoints"] == 20
    assert result["failures"] == []
    assert result["independent_elf_sha_attested_here"] is False

    with tempfile.TemporaryDirectory(prefix="m205-host-") as temp:
        work = Path(temp)
        manifest = work / "manifest.tsv"
        manifest.write_text(CANDIDATES.read_text())
        first = work / "first.serial"
        second = work / "reopen.serial"
        target = work / "report.json"
        first.write_text(clean_first)
        second.write_text(clean_reopen)
        base = [sys.executable, str(MODULE), str(manifest), str(first),
                str(second), str(target)]
        ok = subprocess.run(base + ["--require-all"], capture_output=True,
                            text=True, check=False)
        assert ok.returncode == 0, ok.stderr
        assert "20/20" in ok.stdout
        assert json.loads(target.read_text())["total_entrypoints"] == 20

        package = cases[0]["package"]
        path = cases[0]["elf_path"]
        install_fail = {(package, path, "install"): "FAIL",
                        (package, path, "functional"): "BLOCKED"}
        reboot_fail = {(package, path, "reopen"): "BLOCKED"}
        failure_first = uart(cases, "first", install_fail)
        failure_reopen = uart(cases, "reopen", reboot_fail)
        partial = build_report(cases, failure_first + "[MISS] syscall nr=409\n",
                               failure_reopen)
        assert partial["newly_installed_full_pass_entrypoints"] == 19
        assert partial["failures"] == [path]
        assert partial["unknown_raw_syscalls_observed"]["first"] == {"409": 1}
        first.write_text(failure_first)
        second.write_text(failure_reopen)
        rejected = subprocess.run(base + ["--require-all"], capture_output=True,
                                  text=True, check=False)
        assert rejected.returncode == 1, rejected.stderr
        assert "STRICT FAIL" in rejected.stdout
        discovery = subprocess.run(base, capture_output=True, text=True,
                                   check=False)
        assert discovery.returncode == 0 and "19/20" in discovery.stdout

        # An already-present package is not a new official install, no
        # matter how many paths the package provided or whether it runs.
        preexisting = {(package, path, "install"): "PREEXISTING"}
        prior = build_report(cases, uart(cases, "first", preexisting), clean_reopen)
        assert prior["preexisting"] == 1
        assert prior["newly_installed_full_pass_entrypoints"] == 19

        raises("contradictory", build_report, cases, failure_first, clean_reopen)
        raises("contradictory", build_report, cases,
               uart(cases, "first", {(package, path, "functional"): "BLOCKED"}),
               clean_reopen)
        raises("invalid install", build_report, cases,
               uart(cases, "first", {(package, path, "install"): "BLOCKED"}),
               clean_reopen)
        raises("invalid functional", build_report, cases,
               uart(cases, "first", {(package, path, "functional"): "PREEXISTING"}),
               clean_reopen)
        raises("invalid second-boot", build_report, cases, clean_first,
               uart(cases, "reopen", {(package, path, "reopen"): "PREEXISTING"}))

        raises("missing/extra", build_report, cases,
               "\n".join(clean_first.splitlines()[1:]) + "\n", clean_reopen)
        raises("missing/extra", build_report, cases, clean_first,
               "\n".join(clean_reopen.splitlines()[1:]) + "\n")
        raises("duplicate M205", build_report, cases,
               clean_first + clean_first.splitlines()[0] + "\n", clean_reopen)
        raises("missing/duplicate", build_report, cases,
               clean_first.replace("M205_FIRST_DISCOVERY_COMPLETE", ""), clean_reopen)
        raises("missing/duplicate", build_report, cases,
               clean_first + "M205_FIRST_DISCOVERY_COMPLETE\n", clean_reopen)
        raises("missing/duplicate", build_report, cases, clean_first,
               clean_reopen.replace("M205_REOPEN_DISCOVERY_COMPLETE", ""))
        raises("unexpected", parse_uart,
               clean_first.replace("\tinstall\t", "\treopen\t", 1), "first")
        raises("malformed", parse_uart,
               clean_first.replace("\tfunctional\tPASS", "\tfunctional\tPASS\textra", 1),
               "first")
        raises("unsupported", parse_uart,
               clean_first.replace("\tfunctional\tPASS", "\tfunctional\tSKIPPED", 1),
               "first")
        raises("missing/extra", build_report, cases,
               clean_first + "M205_RESULT\trogue\tbin/rogue\tinstall\tPASS\n",
               clean_reopen)

        error = f"M205_ERROR\t{package}\t{path}\tfunctional\tproof of error\n"
        with_error = build_report(cases, clean_first + error, clean_reopen)
        assert with_error["software"][0]["diagnostics"]["functional"] == [
            "proof of error"]
        raises("unknown", build_report, cases,
               clean_first + error.replace("\tfunctional\t", "\treopen\t"), clean_reopen)
        raises("too many", build_report, cases, clean_first + error * 5, clean_reopen)
        raises("invalid M205 error", build_report, cases,
               clean_first + error.replace("proof of error", "x" * 181), clean_reopen)
        raises("malformed M205 error", build_report, cases,
               clean_first + "M205_ERROR\tincomplete\n", clean_reopen)

        malformed = manifest.read_text()
        manifest.write_text(malformed.replace("package\telf_path", "pkg\telf_path", 1))
        raises("header", manifest_cases, manifest)
        manifest.write_text(malformed + malformed.splitlines()[-1] + "\n")
        raises("duplicate ELF path", manifest_cases, manifest)
        manifest.write_text(malformed.replace("usr/bin/named-compilezone", "../bin/fake", 1))
        raises("invalid case", manifest_cases, manifest)
        manifest.write_text("package\telf_path\tfamily\tprobe\n")
        raises("requires 20", manifest_cases, manifest)

    print("[M205 HOST CONTRACT PASS] 20 individually accounted signed-ELF candidates, "
          "three M204/M205 fd9 stdin-theft regressions, strict first/reopen records, "
          "dedup and status consistency, negative-proof "
          "cases; QEMU and unique SHA remain external acceptance gates")


if __name__ == "__main__":
    main()
