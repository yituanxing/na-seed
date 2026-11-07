#!/usr/bin/env python3
"""Strict three original OpenSSH ELF guest ledger; host/wire checks mandatory."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

source = Path(__file__).with_name('m205-compat-report.py')
spec = spec_from_file_location('m222_m205_report', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M222 requires existing M205 signed ledger')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 3
owner.RESULT = 'M222_RESULT\t'
owner.ERROR = 'M222_ERROR\t'
owner.MARKERS = {'first': 'M222_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M222_REOPEN_DISCOVERY_COMPLETE'}


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
        report['milestone'] = 'M222'
        count = report['newly_installed_full_pass_entrypoints']
        report['acceptance'] = (
            'cli-ledger-only-real-host-sshd-and-two-qemu-pcaps-required'
            if args.require_all and count == 3 and report['preexisting'] == 0
            else 'discovery-not-full-compatibility')
        args.json_output.write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as exc:
        parser.error(f'[M222 INVALID GUEST LEDGER] {exc}')
    print(f'[M222 GUEST CLI LEDGER] {count}/3 original signed SSH/SCP/SFTP first+reopen; independent authenticated OpenSSH host/wire still mandatory')
    return 0 if not args.require_all or (count == 3 and report['preexisting'] == 0) else 1


if __name__ == '__main__':
    raise SystemExit(main())
