#!/usr/bin/env python3
"""Twelve signed original Alpine ELFs on M197's actual native-QEMU UART."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m235_uart_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M235 requires the existing Alpine QEMU UART owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M235_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M235_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     'M235_COHORT_SCRIPT_RC=0',
     '12 original signed Alpine ELF functional probes in one QEMU'),
    ("test -s /root/m235-first-results.tsv && sync; "
     "printf 'M235_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     'M235_DISCOVERY_SYNC_RC=0',
     'durable exact 12 original-Alpine first-boot results'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m235-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M235_PUBLIC_SECONDS', '4400'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M235',
        success_message='[M235 FIRST GUEST RECORDED] 12 original signed Alpine ELF semantics, no inferred PASS')


if __name__ == '__main__':
    main()
