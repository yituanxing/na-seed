#!/usr/bin/env python3
"""M225 first native QEMU: original signed APK install and two FTP clients."""
from importlib.util import module_from_spec, spec_from_file_location
import os
from pathlib import Path
import sys

spec = spec_from_file_location('m225_m205_uart', Path(__file__).with_name('m205-alpine-uart.py'))
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ('NAS_M225_BOOT_PHASE=first sh /root/m225-software-workload.sh --first; '
     'printf \'M225_COHORT_SCRIPT_RC=%d\\n\' "$?"\r',
     'M225_COHORT_SCRIPT_RC=0', 'two original signed FTP clients'),
    ('test -s /root/m225-first-results.tsv && sync; '
     'printf \'M225_DISCOVERY_SYNC_RC=%d\\n\' "$?"\r',
     'M225_DISCOVERY_SYNC_RC=0', 'durable native guest FTP ledger'),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit('usage: m225-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M225_PUBLIC_SECONDS', '2700'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M225',
        success_message='[M225 ORIGINAL FTP CLI RECORDED] genuine host FTP '
                        'PCAP/physical ext4/signed SHA/two native boots still required')


if __name__ == '__main__':
    main()
