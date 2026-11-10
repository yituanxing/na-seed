#!/usr/bin/env python3
"""M238 shares M197's single genuine Alpine first-QEMU UART owner."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import os
import sys

source = Path(__file__).with_name('m205-alpine-uart.py')
spec = spec_from_file_location('m238_uart_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M238 requires existing native QEMU UART owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)

COMMANDS = owner.m204.M197.build_commands(cache_index=True) + (
    ("NAS_M238_COMPAT_COHORT=1 sh /root/m205-software-workload.sh --first; "
     "printf 'M238_COHORT_SCRIPT_RC=%d\\n' \"$?\"\r",
     'M238_COHORT_SCRIPT_RC=0',
     'fifteen original signed Alpine ELF functional probes in one QEMU'),
    ("test -s /root/m238-first-results.tsv && sync; "
     "printf 'M238_DISCOVERY_SYNC_RC=%d\\n' \"$?\"\r",
     'M238_DISCOVERY_SYNC_RC=0',
     'durable M238 first-QEMU functional result ledger'),
)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit('usage: m238-alpine-uart.py RUNNER SERIAL RUNNER_LOG FIFO')
    seconds = int(os.environ.get('NAS_M238_PUBLIC_SECONDS', '4400'))
    owner.m204.M197.M194.run(
        sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]),
        seconds + 35, commands=COMMANDS, stage='M238',
        success_message='[M238 FIRST GUEST RECORDED] fifteen original signed APK ELF semantics, not inferred PASS')


if __name__ == '__main__':
    main()
