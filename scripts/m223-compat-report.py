#!/usr/bin/env python3
"""Strict original signed HTTP 3-ELF two-boot guest ledger, not HTTP oracle."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name('m205-compat-report.py')
spec = spec_from_file_location('m223_m205_report', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M223 requires established M205 signed guest ledger')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 3
owner.RESULT = 'M223_RESULT\t'
owner.ERROR = 'M223_ERROR\t'
owner.MARKERS = {'first': 'M223_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M223_REOPEN_DISCOVERY_COMPLETE'}


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
        report['milestone'] = 'M223'
        count = report['newly_installed_full_pass_entrypoints']
        report['acceptance'] = ('cli-ledger-only-independent-real-host-http-pcap-'
                                'and-physical-disk-sha-required'
                                if args.require_all and count == 3 and report['preexisting'] == 0
                                else 'discovery-not-full-compatibility')
        args.json_output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.error(f'[M223 INVALID GUEST LEDGER] {exc}')
    print(f'[M223 GUEST CLI LEDGER] {count}/3 original signed wget/aria2c/lighttpd '
          'first+reopen; independent host real HTTP/PCAP/disk still mandatory')
    return 0 if not args.require_all or (count == 3 and report['preexisting'] == 0) else 1


if __name__ == '__main__':
    raise SystemExit(main())
