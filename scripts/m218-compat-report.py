#!/usr/bin/env python3
"""Fail-closed first/reopen report for one new original Alpine ifstat ELF."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name("m205-compat-report.py")
spec = spec_from_file_location("m218_m205_report", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M218 requires the existing M205 evidence parser")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 1
owner.RESULT = "M218_RESULT\t"
owner.ERROR = "M218_ERROR\t"
owner.MARKERS = {"first": "M218_FIRST_DISCOVERY_COMPLETE",
                 "reopen": "M218_REOPEN_DISCOVERY_COMPLETE"}


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
        report["milestone"] = "M218"
        passed = report["newly_installed_full_pass_entrypoints"]
        report["acceptance"] = (
            "strict-native-first-and-reopen-1-of-1" if
            args.require_all and passed == 1 and report["preexisting"] == 0
            else "discovery-not-full-compatibility")
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M218 INVALID EVIDENCE] {exc}")
    print(f"[M218 PILOT REPORT] {passed}/1 original signed Alpine ifstat "
          "first+independent reopen; preexisting=" + str(report["preexisting"]))
    for case in report["software"]:
        print(f"[M218 ENTRY] {case['elf_path']} install={case['install']} "
              f"functional={case['functional']} reopen={case['reopen']}")
    if args.require_all and (passed != 1 or report["preexisting"]):
        print("[M218 STRICT FAIL] missing, preinstalled or failed original ifstat")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
