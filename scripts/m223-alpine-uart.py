#!/usr/bin/env python3
"""First native QEMU original Alpine HTTP cohort; not a host/PCAP PASS."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m223_m205_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M223 requires existing M205 signed first-QEMU UART')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ('NAS_M223_PILOT=1 sh /root/m205-software-workload.sh --first; '
     'printf \'M223_COHORT_SCRIPT_RC=%d\\n\' "$?"\r',
     'M223_COHORT_SCRIPT_RC=0', 'three original Alpine HTTP semantic probes'),
    ('test -s /root/m223-first-results.tsv && sync; '
     'printf \'M223_DISCOVERY_SYNC_RC=%d\\n\' "$?"\r',
     'M223_DISCOVERY_SYNC_RC=0', 'durable original HTTP cohort ledger'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m223-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M223_PUBLIC_SECONDS', '2500'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M223',
        success_message='[M223 ALPINE HTTP CLI RECORDED] host-random HTTP, two '
                        'independent PCAPs, physical ext4 oracle still mandatory')


if __name__ == '__main__':
    main()
