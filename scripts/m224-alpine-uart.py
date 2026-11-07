#!/usr/bin/env python3
"""Original Alpine TCP cohort first real QEMU UART; never host/wire PASS."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m224_m205_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M224 requires existing signed M205 UART owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ('NAS_M224_PILOT=1 sh /root/m205-software-workload.sh --first; '
     'printf \'M224_COHORT_SCRIPT_RC=%d\\n\' "$?"\r',
     'M224_COHORT_SCRIPT_RC=0', 'three genuine Alpine TCP semantic probes'),
    ('test -s /root/m224-first-results.tsv && sync; '
     'printf \'M224_DISCOVERY_SYNC_RC=%d\\n\' "$?"\r',
     'M224_DISCOVERY_SYNC_RC=0', 'durable original TCP signed ledger'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m224-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M224_PUBLIC_SECONDS', '2500'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M224',
        success_message='[M224 ALPINE TCP CLI RECORDED] true TCP host/PCAP '
                        'and physical ext4 original SHA oracle still required')


if __name__ == '__main__':
    main()
