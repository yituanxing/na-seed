#!/usr/bin/env python3
"""Original Alpine PID1 and original Mosquitto/OpenRC host MQTT gates."""
import importlib.util
import os
from pathlib import Path
import sys

source = Path(__file__).with_name('m227-alpine-uart.py')
spec = importlib.util.spec_from_file_location('m231_m227_uart', source)
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)

FIRST_COMMANDS = owner.FIRST_COMMANDS[:-2] + (
    ('if apk info -e mosquitto >/dev/null 2>&1; '
     'then echo M231_SIGNED_MQTT_PREEXISTS; else echo M231_SIGNED_MQTT_ABSENT; fi\r',
     'M231_SIGNED_MQTT_ABSENT', 'original broker absent before HTTPS APK'),
    ('apk add mosquitto mosquitto-clients mosquitto-openrc; '
     'printf \'M231_SIGNED_MQTT_APK_RC=%d\\n\' "$?"\r',
     'M231_SIGNED_MQTT_APK_RC=0', 'three official signed Alpine MQTT packages'),
    ('sh /root/m231-alpine-mosquitto-workload.sh --first; '
     'printf \'M231_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
     'M231_FIRST_SCRIPT_RC=0', 'original Mosquitto/OpenRC real inbound MQTT'),
    ('sync; printf \'M231_FIRST_SYNC_RC=%d\\n\' "$?"\r',
     'M231_FIRST_SYNC_RC=0', 'first physical ext4 MQTT DB sync'),
)
REOPEN_COMMANDS = owner.REBOOT_COMMANDS[:1] + (
    ('sh /root/m231-alpine-mosquitto-workload.sh --reopen; '
     'printf \'M231_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
     'M231_REOPEN_SCRIPT_RC=0', 'original OpenRC automatic broker recovery'),
    ('sync; printf \'M231_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
     'M231_REOPEN_SYNC_RC=0', 'second ext4 persisted MQTT DB sync'),
)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reopen'):
        raise SystemExit('usage: m231-alpine-uart.py first|reopen RUNNER SERIAL RUNNER_LOG FIFO')
    phase = sys.argv[1]
    commands = FIRST_COMMANDS if phase == 'first' else REOPEN_COMMANDS
    budget = int(os.environ.get('NAS_M231_FIRST_SECONDS' if phase == 'first'
                                else 'NAS_M231_REOPEN_SECONDS',
                                '2700' if phase == 'first' else '1000'))
    owner.owner.M194.run(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
                         Path(sys.argv[5]), budget + 35, commands=commands,
                         stage='M231-' + phase.upper(),
                         login_hostnames=(('naseed', 'localhost') if phase == 'reopen'
                                          else ('naseed',)),
                         success_message='[M231 ' + phase.upper() +
                         ' UART PASS] genuine Alpine PID1/OpenRC/Mosquitto; '
                         'host MQTT+PCAP/ext4 still REQUIRED')


if __name__ == '__main__':
    main()
