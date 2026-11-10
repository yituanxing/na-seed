#!/usr/bin/env python3
"""M230 official Alpine Dropbear/OpenRC two genuine PID1 SSH server boots."""

import importlib.util
import os
from pathlib import Path
import sys


source = Path(__file__).with_name('m227-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m230_m227_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M230 requires the original M227 OpenRC UART owner')
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


# Retain M197's signed original Alpine packages and M227's original OpenRC
# lighttpd installation; only the new Dropbear package/service is added.
# M227's workload is invoked by M230's workload after signed APK installation.
FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('if apk info -e dropbear >/dev/null 2>&1 || '
     'apk info -e dropbear-openrc >/dev/null 2>&1; '
     'then echo M230_SIGNED_DROPBEAR_PREEXISTS; '
     'else echo M230_SIGNED_DROPBEAR_ABSENT; fi\r',
     'M230_SIGNED_DROPBEAR_ABSENT', 'original Dropbear absent before signed APK'),
    ('apk add dropbear dropbear-openrc; '
     'printf \'M230_SIGNED_DROPBEAR_APK_RC=%d\\n\' "$?"\r',
     'M230_SIGNED_DROPBEAR_APK_RC=0', 'original signed Dropbear/OpenRC packages'),
    ('sh /root/m230-alpine-dropbear-workload.sh --first; '
     'printf \'M230_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M230_FIRST_SCRIPT_RC=0', 'original Dropbear SSH server and host nonce'),
    ('sync; printf \'M230_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M230_FIRST_SYNC_RC=0', 'first genuine ext4 SSH identity sync'),
)

REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m230-alpine-dropbear-workload.sh --reopen; '
     'printf \'M230_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M230_REOPEN_SCRIPT_RC=0', 'second original PID1 automatic SSH recovery'),
    ('sync; printf \'M230_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M230_REOPEN_SYNC_RC=0', 'second real physical ext4 SSH identity sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m230-alpine-uart.py first|reopen '
                         'RUNNER SERIAL RUNNER_LOG FIFO')
    mode = sys.argv[1]
    commands = FIRST_COMMANDS if mode == 'first' else REOPEN_COMMANDS
    seconds = int(os.environ.get('NAS_M230_FIRST_SECONDS' if mode == 'first'
                                 else 'NAS_M230_REOPEN_SECONDS',
                                 '2700' if mode == 'first' else '1150'))
    owner.owner.M194.run(
        sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        seconds + 35, commands=commands, stage=f'M230-{mode.upper()}',
        login_hostnames=(('naseed', 'localhost') if mode == 'reopen'
                         else ('naseed',)),
        success_message=f'[M230 {mode.upper()} UART PASS] original Alpine '
                        'signed Dropbear/OpenRC PID1 SSH server challenge '
                        'window; independent host OpenSSH receipt, PCAP '
                        'and physical ext4 still REQUIRED')


if __name__ == '__main__':
    main()
