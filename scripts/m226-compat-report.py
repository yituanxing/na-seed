#!/usr/bin/env python3
"""M137's old signed restic ELF: NEW REST remote/two-boot guest ledger only."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path

spec = spec_from_file_location('m226_original_ledger', Path(__file__).with_name('m205-compat-report.py'))
owner = module_from_spec(spec)
spec.loader.exec_module(owner)
owner.EXPECTED_COUNT = 1
owner.RESULT = 'M226_RESULT\t'
owner.ERROR = 'M226_ERROR\t'
owner.MARKERS = {'first': 'M226_FIRST_DISCOVERY_COMPLETE',
                 'reopen': 'M226_REOPEN_DISCOVERY_COMPLETE'}


def main():
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
        result['milestone'] = 'M226'
        result['new_original_elf_count'] = 0  # Already proved under M137.
        passed = result['newly_installed_full_pass_entrypoints']
        result['acceptance'] = ('guest-restic-ledger-only-external-real-REST-PCAP-ext4-required'
                                if passed == 1 and result['preexisting'] == 0
                                else 'discovery-not-full-compatibility')
        args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    except (OSError, ValueError) as error:
        parser.error(f'[M226 SIGNED REST GUEST LEDGER FAIL] {error}')
    print(f'[M226 REST GUEST LEDGER] {passed}/1 M137 historical signed restic, '
          'first/reopen remote REST semantics (0 new ELF identities)')
    return 0 if not args.require_all or (passed == 1 and result['preexisting'] == 0) else 1


if __name__ == '__main__':
    raise SystemExit(main())
