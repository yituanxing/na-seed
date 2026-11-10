#!/usr/bin/env python3
"""Original signed SSH command/SCP/SFTP on first public Alpine QEMU boot."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m222_m205_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M222 requires unchanged M205 signed first-QEMU UART')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ('NAS_M222_PILOT=1 sh /root/m205-software-workload.sh --first; '
     'printf \'M222_COHORT_SCRIPT_RC=%d\\n\' "$?"\r',
     'M222_COHORT_SCRIPT_RC=0', 'three genuine original signed SSH clients'),
    ('test -s /root/m222-first-results.tsv && sync; '
     'printf \'M222_DISCOVERY_SYNC_RC=%d\\n\' "$?"\r',
     'M222_DISCOVERY_SYNC_RC=0', 'durable original OpenSSH three-ELF ledger'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m222-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M222_PUBLIC_SECONDS', '2400'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M222',
        success_message='[M222 ALPINE SSH CLI RECORDED] independent SSH host authentication, '
                        'SCP/SFTP host echoes and two QEMU PCAPs still mandatory')


if __name__ == '__main__':
    main()
