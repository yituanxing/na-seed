#!/usr/bin/env python3
"""M228 real host ingress, reusing M227's authenticated two native PID1 boots."""
import importlib.util
import os
from pathlib import Path
import sys


source = Path(__file__).with_name('m227-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m228_m227_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M228 requires original M227 signed OpenRC UART')
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


# The M197 public-HTTPS commands, M227 signed four-package install and
# original OpenRC setup are reused; only the final guest workload exposes
# the listener to the physical eth0 during a bounded independent host GET.
FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('sh /root/m228-alpine-host-ingress-workload.sh --first; '
     'printf \'M228_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M228_FIRST_SCRIPT_RC=0',
     'first original OpenRC service and host TCP GET observation window'),
    ('sync; printf \'M228_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M228_FIRST_SYNC_RC=0', 'first true QEMU physical ext4 sync'),
)
REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m228-alpine-host-ingress-workload.sh --reopen; '
     'printf \'M228_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M228_REOPEN_SCRIPT_RC=0',
     'second original PID1/OpenRC auto-restore and host GET window'),
    ('sync; printf \'M228_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M228_REOPEN_SYNC_RC=0', 'second true QEMU physical ext4 sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m228-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    mode = sys.argv[1]
    commands = FIRST_COMMANDS if mode == 'first' else REOPEN_COMMANDS
    seconds = int(os.environ.get('NAS_M228_FIRST_SECONDS' if mode == 'first'
                                 else 'NAS_M228_REOPEN_SECONDS',
                                 '2500' if mode == 'first' else '900'))
    owner.owner.M194.run(
        sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        seconds + 35, commands=commands, stage=f'M228-{mode.upper()}',
        login_hostnames=(('naseed', 'localhost') if mode == 'reopen'
                         else ('naseed',)),
        success_message=f'[M228 {mode.upper()} UART PASS] signed original '
                        'Alpine /sbin/init, password-authenticated OpenRC '
                        'and real guest ingress-ready window; independent '
                        'host HTTP/PCAP/ext4 proof still REQUIRED')


if __name__ == '__main__':
    main()
