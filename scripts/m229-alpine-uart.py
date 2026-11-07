#!/usr/bin/env python3
"""M229 original signed Alpine Valkey/OpenRC and real host RESP dual boot."""
import importlib.util
import os
from pathlib import Path
import sys


source = Path(__file__).with_name('m227-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m229_m227_uart', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M229 requires the original M227 OpenRC UART owner')
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


# Reuse the full M197 official HTTPS signed four-app installation and M227's
# *original* lighttpd/OpenRC APK transaction.  The M229 script calls the same
# proven M227 networking/openrc workload; only the additional original Valkey
# service and real host-to-guest RESP proof are new.
FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('if apk info -e valkey >/dev/null 2>&1 || '
     'apk info -e valkey-cli >/dev/null 2>&1 || '
     'apk info -e valkey-openrc >/dev/null 2>&1; '
     'then echo M229_SIGNED_VALKEY_PREEXISTS; '
     'else echo M229_SIGNED_VALKEY_ABSENT; fi\r',
     'M229_SIGNED_VALKEY_ABSENT', 'original Valkey service absent before APK'),
    ('apk add valkey valkey-cli valkey-openrc; '
     'printf \'M229_SIGNED_VALKEY_APK_RC=%d\\n\' "$?"\r',
     'M229_SIGNED_VALKEY_APK_RC=0', 'signed public Valkey and original OpenRC init'),
    ('sh /root/m229-alpine-valkey-workload.sh --first; '
     'printf \'M229_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M229_FIRST_SCRIPT_RC=0', 'real original Valkey OpenRC and host challenge window'),
    ('sync; printf \'M229_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M229_FIRST_SYNC_RC=0', 'first real physical ext4 Valkey RDB sync'),
)

REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m229-alpine-valkey-workload.sh --reopen; '
     'printf \'M229_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M229_REOPEN_SCRIPT_RC=0', 'second original PID1 auto-start and durable RESP'),
    ('sync; printf \'M229_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M229_REOPEN_SYNC_RC=0', 'second real physical ext4 Valkey RDB sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m229-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    mode = sys.argv[1]
    commands = FIRST_COMMANDS if mode == 'first' else REOPEN_COMMANDS
    seconds = int(os.environ.get('NAS_M229_FIRST_SECONDS' if mode == 'first'
                                 else 'NAS_M229_REOPEN_SECONDS',
                                 '2500' if mode == 'first' else '900'))
    owner.owner.M194.run(
        sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5]),
        seconds + 35, commands=commands, stage=f'M229-{mode.upper()}',
        login_hostnames=(('naseed', 'localhost') if mode == 'reopen'
                         else ('naseed',)),
        success_message=f'[M229 {mode.upper()} UART PASS] signed Alpine '
                        'Valkey/OpenRC on original PID1 and real inbound RESP '
                        'window; host receipt/physical PCAP/ext4 still REQUIRED')


if __name__ == '__main__':
    main()
