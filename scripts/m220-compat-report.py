#!/usr/bin/env python3
"""Strict signed guest M220 ledger; independent host wire gate is separate."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name("m205-compat-report.py")
spec = spec_from_file_location("m220_m205_report", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M220 requires the existing M205 ledger parser")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 1
owner.RESULT = "M220_RESULT\t"
owner.ERROR = "M220_ERROR\t"
owner.MARKERS = {"first": "M220_FIRST_DISCOVERY_COMPLETE",
                 "reopen": "M220_REOPEN_DISCOVERY_COMPLETE"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("first_uart", type=Path)
    parser.add_argument("reopen_uart", type=Path)
    parser.add_argument("json_output", type=Path)
    parser.add_argument("--require-all", action="store_true")
    args = parser.parse_args()
    try:
        report = owner.build_report(
            owner.manifest_cases(args.manifest),
            args.first_uart.read_text(errors="replace"),
            args.reopen_uart.read_text(errors="replace"))
        report["milestone"] = "M220"
        passed = report["newly_installed_full_pass_entrypoints"]
        # This proves ledger alone; this field never asserts UDP wire PASS.
        report["acceptance"] = (
            "cli-ledger-only-host-wire-gate-required" if
            args.require_all and passed == 1 and report["preexisting"] == 0
            else "discovery-not-full-compatibility")
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M220 INVALID GUEST LEDGER] {exc}")
    print(f"[M220 GUEST CLI LEDGER] {passed}/1 first+reopen; actual UDP requires both independent wire oracles")
    if args.require_all and (passed != 1 or report["preexisting"]):
        print("[M220 STRICT FAIL] missing original signed ELF or incomplete guest ledger")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
