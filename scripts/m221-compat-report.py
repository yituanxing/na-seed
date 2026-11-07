#!/usr/bin/env python3
"""Strict original two-ELF Alpine guest ledger, separate from host wire gate."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name("m205-compat-report.py")
spec = spec_from_file_location("m221_m205_report", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M221 requires existing M205 strict signed guest ledger")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 2
owner.RESULT = "M221_RESULT\t"
owner.ERROR = "M221_ERROR\t"
owner.MARKERS = {"first": "M221_FIRST_DISCOVERY_COMPLETE",
                 "reopen": "M221_REOPEN_DISCOVERY_COMPLETE"}


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
        report["milestone"] = "M221"
        count = report["newly_installed_full_pass_entrypoints"]
        report["acceptance"] = (
            "cli-ledger-only-authoritative-host-and-pcap-required"
            if args.require_all and count == 2 and report["preexisting"] == 0
            else "discovery-not-full-compatibility")
        args.json_output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M221 INVALID GUEST LEDGER] {exc}")
    print(f"[M221 GUEST CLI LEDGER] {count}/2 signed original nsupdate+host first+reopen; independent DNS wire mandatory")
    if args.require_all and (count != 2 or report["preexisting"]):
        print("[M221 STRICT FAIL] original signed ELF or guest ledger incomplete")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
