#!/usr/bin/env python3
"""M226 genuine signed Alpine restic, remote REST, first native QEMU."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

spec = spec_from_file_location('m226_m205_uart', Path(__file__).with_name('m205-alpine-uart.py'))
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ('NAS_M226_BOOT_PHASE=first sh /root/m226-software-workload.sh --first; '
     'printf \'M226_COHORT_SCRIPT_RC=%d\\n\' "$?"\r',
     'M226_COHORT_SCRIPT_RC=0', 'signed restic original against true REST repository'),
    ('test -s /root/m226-first-results.tsv && sync; '
     'printf \'M226_DISCOVERY_SYNC_RC=%d\\n\' "$?"\r',
     'M226_DISCOVERY_SYNC_RC=0', 'durable original native guest remote restic ledger'),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit('usage: m226-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M226_PUBLIC_SECONDS', '3600'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M226',
        success_message='[M226 ORIGINAL RESTIC CLI RECORDED] host upstream server, '
                        'two native QEMUs, PCAP/ext4 and exact APK SHA pending')


if __name__ == '__main__':
    main()
