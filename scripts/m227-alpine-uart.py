#!/usr/bin/env python3
"""M197 original public Alpine + M199 dual genuine PID1, headless only."""
import importlib.util
import os
from pathlib import Path
import sys


source = Path(__file__).with_name('m197-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m227_m197', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M227 requires the existing original Alpine M197 UART')
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)

FIRST_COMMANDS = owner.build_commands(cache_index=True) + (
    ('if apk info -e lighttpd-openrc >/dev/null 2>&1; '
     'then echo M227_OPENRC_PACKAGE_PREEXISTS; '
     'else echo M227_OPENRC_PACKAGE_ABSENT; fi\r',
     'M227_OPENRC_PACKAGE_ABSENT', 'unmodified official Alpine service absent'),
    ('apk add wget aria2 lighttpd lighttpd-openrc; '
     'printf \'M227_SIGNED_PUBLIC_APK_RC=%d\\n\' "$?"\r',
     'M227_SIGNED_PUBLIC_APK_RC=0', 'official signed four-package HTTP install'),
    ('sh /root/m227-alpine-openrc-workload.sh --first; '
     'printf \'M227_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M227_FIRST_SCRIPT_RC=0', 'OpenRC registration, server and two HTTP clients'),
    ('sync; printf \'M227_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M227_FIRST_SYNC_RC=0', 'first native guest ext4 sync'),
)

REBOOT_COMMANDS = (
    ('rc-status; printf \'M227_SECOND_OPENRC_RC=%d\\n\' "$?"\r',
     'M227_SECOND_OPENRC_RC=0', 'second original Alpine PID1/OpenRC boot'),
    ('sh /root/m227-alpine-openrc-workload.sh --reopen; '
     'printf \'M227_SECOND_SCRIPT_RC=%d\\n\' "$?"\r',
     'M227_SECOND_SCRIPT_RC=0', 'automatic lighttpd and persisted HTTP/SQLite/Git'),
    ('sync; printf \'M227_SECOND_SYNC_RC=%d\\n\' "$?"\r',
     'M227_SECOND_SYNC_RC=0', 'second native guest ext4 sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m227-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    mode = sys.argv[1]
    commands = FIRST_COMMANDS if mode == 'first' else REBOOT_COMMANDS
    seconds = int(os.environ.get('NAS_M227_FIRST_SECONDS' if mode == 'first'
                                 else 'NAS_M227_REOPEN_SECONDS',
                                 '1650' if mode == 'first' else '500'))
    owner.M194.run(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
                   Path(sys.argv[5]), seconds + 35, commands=commands,
                   stage=f'M227-{mode.upper()}',
                   success_message=f'[M227 {mode.upper()} UART PASS] original '
                                   'Alpine PID1, signed applications and '
                                   'OpenRC service; independent host ext4 '
                                   'and two-boot evidence still mandatory')


if __name__ == '__main__':
    main()
