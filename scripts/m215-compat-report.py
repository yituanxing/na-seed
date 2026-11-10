#!/usr/bin/env python3
"""Strict M215 five-ELF first/reopen evidence via existing M205 parser."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name("m205-compat-report.py")
spec = spec_from_file_location("m215_m205_report", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M215 requires the existing M205 evidence parser")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 5
owner.RESULT = "M215_RESULT\t"
owner.ERROR = "M215_ERROR\t"
owner.MARKERS = {"first": "M215_FIRST_DISCOVERY_COMPLETE",
                 "reopen": "M215_REOPEN_DISCOVERY_COMPLETE"}


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
        report["milestone"] = "M215"
        passed = report["newly_installed_full_pass_entrypoints"]
        report["acceptance"] = (
            "strict-native-first-and-reopen-5-of-5" if
            args.require_all and passed == 5 and report["preexisting"] == 0
            else "discovery-not-full-compatibility")
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M215 INVALID EVIDENCE] {exc}")
    print(f"[M215 PILOT REPORT] {passed}/5 original signed Alpine ELFs "
          "first+independent reopen; preexisting=" + str(report["preexisting"]))
    for case in report["software"]:
        print(f"[M215 ENTRY] {case['elf_path']} install={case['install']} "
              f"functional={case['functional']} reopen={case['reopen']}")
    if args.require_all and (passed != 5 or report["preexisting"]):
        print("[M215 STRICT FAIL] any missing original ELF or preexisting APK is failure")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
