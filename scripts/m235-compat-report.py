#!/usr/bin/env python3
"""Strict fail-closed M235 12/12 first+reopen report, shared M205 parser."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name('m205-compat-report.py')
spec = spec_from_file_location('m235_report_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M235 requires existing QEMU ledger parser')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.RESULT = 'M235_RESULT\t'
owner.ERROR = 'M235_ERROR\t'
owner.EXPECTED_COUNT = 12
owner.MARKERS = {'first': 'M235_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M235_REOPEN_DISCOVERY_COMPLETE'}


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
        report['milestone'] = 'M235'
        report['acceptance'] = ('strict-guest-first-and-reopen-12-of-12'
                                if args.require_all and passed == 12 and report['preexisting'] == 0
                                else 'discovery-not-full-compatibility')
        args.json_output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.error(f'[M235 INVALID EVIDENCE] {exc}')
    print(f'[M235 REPORT] {passed}/12 original signed Alpine ELF semantics first+reopen; '
          f'preexisting={report["preexisting"]}')
    for item in report['software']:
        print(f'[M235 ENTRY] {item["elf_path"]} install={item["install"]} '
              f'functional={item["functional"]} reopen={item["reopen"]}')
    if args.require_all and (passed != 12 or report['preexisting'] != 0 or
                             report['total_entrypoints'] != 12):
        print('[M235 STRICT FAIL] real guest functional evidence incomplete')
        return 1
    if args.require_all:
        print('[M235 STRICT GUEST PASS] 12/12 original Alpine ELF, actual install/function/reopen')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
