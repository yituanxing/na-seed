#!/usr/bin/env python3
"""Real original Alpine PID1/OpenRC memcached for independent host probes."""
import importlib.util
import os
from pathlib import Path
import sys

source = Path(__file__).with_name('m227-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m232_m227_uart', source)
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)

FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('if apk info -e memcached >/dev/null 2>&1; '
     'then echo M232_SIGNED_CACHE_PREEXISTS; else echo M232_SIGNED_CACHE_ABSENT; fi\r',
     'M232_SIGNED_CACHE_ABSENT', 'real original signed memcached not preseeded'),
    ('apk add memcached memcached-openrc; '
     'printf \'M232_SIGNED_CACHE_APK_RC=%d\\n\' "$?"\r',
     'M232_SIGNED_CACHE_APK_RC=0', 'original Alpine signed cache/OpenRC install'),
    ('sh /root/m232-alpine-memcached-workload.sh --first; '
     'printf \'M232_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M232_FIRST_SCRIPT_RC=0', 'real OpenRC memcached external host cache window'),
    ('sync; printf \'M232_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M232_FIRST_SYNC_RC=0', 'actual ext4 OpenRC runlevel sync'),
)
REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m232-alpine-memcached-workload.sh --reopen; '
     'printf \'M232_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M232_REOPEN_SCRIPT_RC=0', 'genuine OpenRC auto-start and volatile cache'),
    ('sync; printf \'M232_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M232_REOPEN_SYNC_RC=0', 'actual ext4 service config after cold reboot'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m232-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    phase = sys.argv[1]
    commands = FIRST_COMMANDS if phase == 'first' else REOPEN_COMMANDS
    budget = int(os.environ.get('NAS_M232_FIRST_SECONDS' if phase == 'first'
                                else 'NAS_M232_REOPEN_SECONDS',
                                '2700' if phase == 'first' else '1000'))
    owner.owner.M194.run(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
                         Path(sys.argv[5]), budget + 35, commands=commands,
                         stage='M232-' + phase.upper(),
                         login_hostnames=(('naseed', 'localhost') if phase == 'reopen'
                                          else ('naseed',)),
                         success_message='[M232 ' + phase.upper() +
                         ' UART PASS] genuine Alpine PID1/OpenRC/memcached; '
                         'external host TCP + NIC PCAP and ext4 still REQUIRED')


if __name__ == '__main__':
    main()
