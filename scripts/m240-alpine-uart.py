#!/usr/bin/env python3
"""M240 original signed Alpine PID1/OpenRC Dropbear interactive SSH PTY."""

import importlib.util
import os
from pathlib import Path
import sys


def load(filename: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError('M240 requires original Alpine OpenRC owner')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M230 = load('m230-alpine-uart.py', 'm240_m230_uart')
FIRST_COMMANDS = M230.FIRST_COMMANDS[:-2] + (
    ('sh /root/m240-alpine-ssh-pty-workload.sh --first; '
     'printf \'M240_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M240_FIRST_SCRIPT_RC=0', 'original signed Alpine Dropbear PTY listener'),
    ('sync; printf \'M240_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M240_FIRST_SYNC_RC=0', 'first physical ext4 SSH PTY sync'),
)
REOPEN_COMMANDS = M230.REOPEN_COMMANDS[:1] + (
    ('sh /root/m240-alpine-ssh-pty-workload.sh --reopen; '
     'printf \'M240_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M240_REOPEN_SCRIPT_RC=0', 'automatic original OpenRC SSH PTY recovery'),
    ('sync; printf \'M240_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M240_REOPEN_SYNC_RC=0', 'second physical ext4 SSH PTY sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m240-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    phase = sys.argv[1]
    seconds = int(os.environ.get('NAS_M240_FIRST_SECONDS' if phase == 'first'
                                 else 'NAS_M240_REOPEN_SECONDS',
                                 '2700' if phase == 'first' else '1150'))
    M230.owner.owner.M194.run(
        sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        seconds + 35, commands=FIRST_COMMANDS if phase == 'first' else REOPEN_COMMANDS,
        stage='M240-' + phase.upper(),
        login_hostnames=(('naseed', 'localhost') if phase == 'reopen' else ('naseed',)),
        success_message=f'[M240 {phase.upper()} UART PASS] original Alpine '
                        'signed Dropbear/OpenRC SSH PTY challenge window; '
                        'independent 9-session host OpenSSH, NIC PCAP and '
                        'physical ext4 evidence still REQUIRED')


if __name__ == '__main__':
    main()
