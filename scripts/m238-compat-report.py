#!/usr/bin/env python3
"""Fail-closed M238 two-boot report, reusing M205's strict guest parser."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name('m205-compat-report.py')
spec = spec_from_file_location('m238_report_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M238 requires existing M205 ledger parser')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.RESULT = 'M238_RESULT\t'
owner.ERROR = 'M238_ERROR\t'
owner.EXPECTED_COUNT = 15
owner.MARKERS = {'first': 'M238_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M238_REOPEN_DISCOVERY_COMPLETE'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('first_uart', type=Path)
    parser.add_argument('reopen_uart', type=Path)
    parser.add_argument('json_output', type=Path)
    parser.add_argument('--require-all', action='store_true')
    args = parser.parse_args()
    try:
        report = owner.build_report(owner.manifest_cases(args.manifest),
                                    args.first_uart.read_text(errors='replace'),
                                    args.reopen_uart.read_text(errors='replace'))
        passed = report['newly_installed_full_pass_entrypoints']
        report['milestone'] = 'M238'
        report['acceptance'] = ('strict-guest-first-and-reopen-15-of-15'
                                if args.require_all and passed == 15 and report['preexisting'] == 0
                                else 'discovery-not-full-compatibility')
        args.json_output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.error(f'[M238 INVALID EVIDENCE] {exc}')
    print(f'[M238 REPORT] {passed}/15 original signed ELF first/reopen; '
          f'preexisting={report["preexisting"]}')
    for item in report['software']:
        print(f'[M238 ENTRY] {item["elf_path"]} install={item["install"]} '
              f'functional={item["functional"]} reopen={item["reopen"]}')
    if args.require_all and (passed != 15 or report['preexisting'] or
                             report['total_entrypoints'] != 15):
        print('[M238 STRICT FAIL] at least one true native functional entry missing')
        return 1
    if args.require_all:
        print('[M238 STRICT GUEST PASS] 15/15 original APK ELF, native first/reopen semantics')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
