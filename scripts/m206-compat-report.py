#!/usr/bin/env python3
"""M206 strict two-native-QEMU reporting using M205's single validated parser.

This wrapper changes the evidence namespace; it does not infer QEMU success.
All PASS/FAIL and duplicate/missing-record checks come from the shared parser.
"""

import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path


source = Path(__file__).with_name("m205-compat-report.py")
spec = spec_from_file_location("m206_m205_report", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M206 requires the proven M205 evidence parser")
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.RESULT = "M206_RESULT\t"
owner.ERROR = "M206_ERROR\t"
owner.MARKERS = {"first": "M206_FIRST_DISCOVERY_COMPLETE",
                 "reopen": "M206_REOPEN_DISCOVERY_COMPLETE"}


def manifest_cases(path: Path) -> list[dict[str, str]]:
    return owner.manifest_cases(path)


def build_report(cases: list[dict[str, str]], first: str, reopen: str) -> dict:
    report = owner.build_report(cases, first, reopen)
    report["milestone"] = "M206"
    # The exact 20 new independent binary hashes are separately required by
    # signed host inventory AND the physical post-reboot ext4 disk oracle.
    report["acceptance"] = "discovery-not-full-compatibility"
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("first_uart", type=Path)
    parser.add_argument("reopen_uart", type=Path)
    parser.add_argument("json_output", type=Path)
    parser.add_argument("--require-all", action="store_true")
    args = parser.parse_args()
    try:
        report = build_report(manifest_cases(args.manifest),
                              args.first_uart.read_text(errors="replace"),
                              args.reopen_uart.read_text(errors="replace"))
        passed = report["newly_installed_full_pass_entrypoints"]
        if (args.require_all and passed == 20 and
                report["total_entrypoints"] == 20 and
                report["preexisting"] == 0):
            # This flag certifies the first/reopen guest oracle only; the
            # signed APK and post-reboot physical ext4 gates run separately.
            report["acceptance"] = "strict-native-first-and-reopen-20-of-20"
        args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    except (OSError, ValueError) as exc:
        parser.error(f"[M206 INVALID EVIDENCE] {exc}")
    print(f"[M206 DISCOVERY REPORT] {passed}/{report['total_entrypoints']} "
          "original signed Alpine programs passed fresh install, real function "
          f"and independent second QEMU; preexisting={report['preexisting']}")
    for item in report["software"]:
        print(f"[M206 ENTRY] {item['elf_path']} install={item['install']} "
              f"functional={item['functional']} reopen={item['reopen']}")
    print(f"[M206 REPORT JSON] {args.json_output}")
    if args.require_all and (passed != 20 or report["preexisting"] != 0):
        print("[M206 STRICT FAIL] discovery cannot count as full compatibility")
        return 1
    if args.require_all:
        print("[M206 STRICT GUEST PASS] 20/20 original signed Alpine software "
              "functional in first and independent second QEMU boot")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
