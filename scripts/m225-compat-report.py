#!/usr/bin/env python3
"""Two original signed Alpine FTP clients' exact first/reopen guest ledger.

The report is only one gate: real host FTP receipts, QEMU PCAP, physical ext4,
official APK SHA, and clean two-QEMU teardown are separately mandatory.
"""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

spec = spec_from_file_location('m225_m205_report', Path(__file__).with_name('m205-compat-report.py'))
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 2
owner.RESULT = 'M225_RESULT\t'
owner.ERROR = 'M225_ERROR\t'
owner.MARKERS = {'first': 'M225_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M225_REOPEN_DISCOVERY_COMPLETE'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('first_uart', type=Path)
    parser.add_argument('reopen_uart', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--require-all', action='store_true')
    args = parser.parse_args()
    try:
        result = owner.build_report(owner.manifest_cases(args.manifest),
                                    args.first_uart.read_text(errors='replace'),
                                    args.reopen_uart.read_text(errors='replace'))
        result['milestone'] = 'M225'
        passed = result['newly_installed_full_pass_entrypoints']
        result['acceptance'] = ('guest-ftp-ledger-only-real-host-pcap-ext4-signed-sha-required'
                                if passed == 2 and result['preexisting'] == 0
                                else 'discovery-not-full-compatibility')
        args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.error(f'[M225 GUEST LEDGER FAIL] {exc}')
    print(f'[M225 FTP GUEST LEDGER] {passed}/2 signed lftp/rclone, '
          'first+reopen; host TCP/PCAP/ext4 not inferred')
    return 0 if not args.require_all or (passed == 2 and result['preexisting'] == 0) else 1


if __name__ == '__main__':
    raise SystemExit(main())
