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


# One authentic signed OpenRC/PID1 UART; original protocol-specific commands retained.
SERVICE_PROFILES = {
    'm228': {
        'first_extra': (
        ('sh /root/m228-alpine-host-ingress-workload.sh --first; '
         'printf \'M228_FIRST_SCRIPT_RC=%d\\n\' "$?"\r',
         'M228_FIRST_SCRIPT_RC=0',
         'first original OpenRC service and host TCP GET observation window'),
        ('sync; printf \'M228_FIRST_SYNC_RC=%d\\n\' "$?"\r',
         'M228_FIRST_SYNC_RC=0', 'first true QEMU physical ext4 sync'),
        ),
        'reopen_extra': (
        ('sh /root/m228-alpine-host-ingress-workload.sh --reopen; '
         'printf \'M228_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
         'M228_REOPEN_SCRIPT_RC=0',
         'second original PID1/OpenRC auto-restore and host GET window'),
        ('sync; printf \'M228_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
         'M228_REOPEN_SYNC_RC=0', 'second true QEMU physical ext4 sync'),
        ),
        'defaults': (2500, 900),
        'success_message': '[M228 {phase} UART PASS] signed original Alpine /sbin/init, password-authenticated OpenRC and real guest ingress-ready window; independent host HTTP/PCAP/ext4 proof still REQUIRED',
    },
    'm229': {
        'first_extra': (
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
        ),
        'reopen_extra': (
        ('sh /root/m229-alpine-valkey-workload.sh --reopen; '
         'printf \'M229_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
         'M229_REOPEN_SCRIPT_RC=0', 'second original PID1 auto-start and durable RESP'),
        ('sync; printf \'M229_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
         'M229_REOPEN_SYNC_RC=0', 'second real physical ext4 Valkey RDB sync'),
        ),
        'defaults': (2500, 900),
        'success_message': '[M229 {phase} UART PASS] signed Alpine Valkey/OpenRC on original PID1 and real inbound RESP window; host receipt/physical PCAP/ext4 still REQUIRED',
    },
    'm231': {
        'first_extra': (
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
        ),
        'reopen_extra': (
        ('sh /root/m231-alpine-mosquitto-workload.sh --reopen; '
         'printf \'M231_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
         'M231_REOPEN_SCRIPT_RC=0', 'original OpenRC automatic broker recovery'),
        ('sync; printf \'M231_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
         'M231_REOPEN_SYNC_RC=0', 'second ext4 persisted MQTT DB sync'),
        ),
        'defaults': (2700, 1000),
        'success_message': '[M231 {phase} UART PASS] genuine Alpine PID1/OpenRC/Mosquitto; host MQTT+PCAP/ext4 still REQUIRED',
    },
    'm232': {
        'first_extra': (
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
        ),
        'reopen_extra': (
        ('sh /root/m232-alpine-memcached-workload.sh --reopen; '
         'printf \'M232_REOPEN_SCRIPT_RC=%d\\n\' "$?"\r',
         'M232_REOPEN_SCRIPT_RC=0', 'genuine OpenRC auto-start and volatile cache'),
        ('sync; printf \'M232_REOPEN_SYNC_RC=%d\\n\' "$?"\r',
         'M232_REOPEN_SYNC_RC=0', 'actual ext4 service config after cold reboot'),
        ),
        'defaults': (2700, 1000),
        'success_message': '[M232 {phase} UART PASS] genuine Alpine PID1/OpenRC/memcached; external host TCP + NIC PCAP and ext4 still REQUIRED',
    },
}


def service_commands(profile: str, phase: str):
    """Original guest argv preserved; independent host protocol gates unchanged."""
    entry = SERVICE_PROFILES[profile]
    if phase == 'first':
        return FIRST_COMMANDS[:-2] + entry['first_extra']
    if phase == 'reopen':
        return REBOOT_COMMANDS[:1] + entry['reopen_extra']
    raise ValueError('unknown original service boot phase')


def main() -> None:
    if len(sys.argv) == 7 and sys.argv[1] in SERVICE_PROFILES:
        profile, mode = sys.argv[1], sys.argv[2]
        if mode not in ('first', 'reopen'):
            raise SystemExit('unsupported OpenRC service boot phase')
        entry = SERVICE_PROFILES[profile]
        default = entry['defaults'][0 if mode == 'first' else 1]
        seconds = int(os.environ.get(f'NAS_{profile.upper()}_{mode.upper()}_SECONDS',
                                     str(default)))
        owner.M194.run(sys.argv[3], Path(sys.argv[4]), Path(sys.argv[5]),
                       Path(sys.argv[6]), seconds + 35,
                       commands=service_commands(profile, mode),
                       stage=f'{profile.upper()}-{mode.upper()}',
                       login_hostnames=(('naseed', 'localhost') if mode == 'reopen'
                                        else ('naseed',)),
                       success_message=entry['success_message'].format(phase=mode.upper()))
        return
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
                   login_hostnames=(('naseed', 'localhost') if mode == 'reopen'
                                    else ('naseed',)),
                   success_message=f'[M227 {mode.upper()} UART PASS] original '
                                   'Alpine PID1, signed applications and '
                                   'OpenRC service; independent host ext4 '
                                   'and two-boot evidence still mandatory')


if __name__ == '__main__':
    main()
