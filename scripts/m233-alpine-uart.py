#!/usr/bin/env python3
"""Two original signed Alpine PID1 boots of original dnsmasq/OpenRC."""
import importlib.util
import os
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location(
    'm233_m227_uart', Path(__file__).with_name('m227-alpine-uart.py'))
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)

FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('if apk info -e dnsmasq >/dev/null 2>&1; '
     'then echo M233_DNSMASQ_PREEXISTS; else echo M233_DNSMASQ_ABSENT; fi\r',
     'M233_DNSMASQ_ABSENT', 'signed DNS server absent before public APK'),
    ('apk add dnsmasq dnsmasq-openrc; '
     'printf \'M233_SIGNED_DNS_APK_RC=%d\\n\' "$?"\r',
     'M233_SIGNED_DNS_APK_RC=0', 'official Alpine dnsmasq and OpenRC APK'),
    ('sh /root/m233-alpine-dnsmasq-workload.sh --first; '
     'printf \'M233_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M233_FIRST_SCRIPT_RC=0', 'original OpenRC DNS server and host UDP+TCP'),
    ('sync; printf \'M233_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M233_FIRST_SYNC_RC=0', 'physical ext4 DNS configuration sync'),
)
REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m233-alpine-dnsmasq-workload.sh --reopen; '
     'printf \'M233_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M233_REOPEN_SCRIPT_RC=0', 'original OpenRC auto-started dnsmasq'),
    ('sync; printf \'M233_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M233_REOPEN_SYNC_RC=0', 'physical ext4 DNS OpenRC persistence'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m233-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    phase = sys.argv[1]
    commands = FIRST_COMMANDS if phase == 'first' else REOPEN_COMMANDS
    budget = int(os.environ.get('NAS_M233_FIRST_SECONDS' if phase == 'first'
                                else 'NAS_M233_REOPEN_SECONDS',
                                '2650' if phase == 'first' else '1000'))
    owner.owner.M194.run(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
                         Path(sys.argv[5]), budget + 35, commands=commands,
                         stage='M233-' + phase.upper(),
                         login_hostnames=(('naseed', 'localhost') if phase == 'reopen'
                                          else ('naseed',)),
                         success_message='[M233 ' + phase.upper() +
                         ' UART PASS] signed Alpine PID1/OpenRC/dnsmasq; '
                         'host UDP+TCP, two PCAP and ext4 still REQUIRED')


if __name__ == '__main__':
    main()
