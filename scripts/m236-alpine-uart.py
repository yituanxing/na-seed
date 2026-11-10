#!/usr/bin/env python3
"""M236 shares M197's single genuine Alpine first-QEMU UART owner."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m236_uart_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M236 requires existing native QEMU UART owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M236_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M236_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     'M236_COHORT_SCRIPT_RC=0',
     'seven original signed Alpine ELF functional probes in one QEMU'),
    ("test -s /root/m236-first-results.tsv && sync; "
     "printf 'M236_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     'M236_DISCOVERY_SYNC_RC=0',
     'durable M236 first-QEMU functional result ledger'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m236-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M236_PUBLIC_SECONDS', '4400'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M236',
        success_message='[M236 FIRST GUEST RECORDED] seven original signed APK ELF semantics, not inferred PASS')


if __name__ == '__main__':
    main()
