#!/usr/bin/env python3
"""Fail-closed static/negative contract for original Alpine Dropbear/OpenRC.

These checks do not replace genuine encrypted host OpenSSH on TWO native QEMU
boots plus physical NIC PCAP and persistent ext4 host-key oracles.
"""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parent.parent
GUEST = ROOT / 'tests/m230-alpine-dropbear-workload.sh'
UART = ROOT / 'scripts/m230-alpine-uart.py'


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError('[M230 GUEST CONTRACT FAIL] ' + message)


def validate(source: str, uart: str) -> None:
    require('set -eu' in source, 'guest must fail closed')
    require('for package in dropbear dropbear-openrc' in source and
            'apk info -e "$package"' in source,
            'both unmodified signed Alpine packages must be present')
    for literal in ('/usr/sbin/dropbear', '/usr/bin/dropbearkey', '7f454c46',
                    '/etc/init.d/dropbear', '/etc/conf.d/dropbear'):
        require(literal in source, 'original Alpine ELF/OpenRC missing: ' + literal)

    for literal in ('/root/m230-authorized-pubkey',
                    '/root/.ssh/authorized_keys',
                    'test ! -L "$pub"',
                    'algorithm" = ssh-ed25519',
                    'restrict ssh-ed25519 %s',
                    'chmod 0700 /root /root/.ssh',
                    'chmod 0600 "$authorized"',
                    'stat -c %a',
                    '/etc/dropbear/dropbear_ed25519_host_key',
                    '/root/m230-dropbear-hostkey.sha256',
                    'sha256sum "$hostkey"'):
        require(literal in source, 'SSH public-key/host-identity contract: ' + literal)

    opts_match = re.search(r'^opts=\'DROPBEAR_OPTS="([^"]+)"\'$', source,
                           re.MULTILINE)
    require(opts_match is not None, 'no isolated authentic Dropbear conf.d options')
    opts = opts_match.group(1).split()
    for flag in ('-s', '-g', '-j', '-k', '-m', '-p', '-r'):
        require(flag in opts, 'Dropbear hardening option missing: ' + flag)
    require('-w' not in opts, 'root-only guest may not disable root pubkey login')
    require(opts[opts.index('-p') + 1] == '0.0.0.0:15446',
            'IPv4-only actual NIC listener must use guest port 15446')
    require(opts[opts.index('-r') + 1] ==
            '/etc/dropbear/dropbear_ed25519_host_key',
            'server must expose persistent Ed25519 host identity')

    require('first original PID1 boot' in source or
            'first original PID1' in source,
            'guest startup must document its owner')
    first = source.split('    FIRST)\n', 1)
    require(len(first) == 2, 'missing first original PID1 case')
    first, reopened = first[1].split('    REOPEN)\n', 1)
    require('/root/m227-alpine-openrc-workload.sh --first' in first,
            'must reuse original signed Alpine M227 networking/OpenRC baseline')
    require('rc-update add dropbear default' in first and
            'rc-service dropbear start' in first,
            'only first boot may register/start vendor OpenRC service')
    require('/root/m227-alpine-openrc-workload.sh --reopen' in reopened,
            'second boot must reuse genuine PID1/OpenRC recovery')
    for forbidden in ('rc-service dropbear start', 'rc-service dropbear restart',
                      '/usr/sbin/dropbear ${DROPBEAR_OPTS}'):
        require(forbidden not in reopened, 'second boot repaired service: ' + forbidden)
    for literal in ('/etc/runlevels/default/dropbear',
                    'rc-service dropbear status',
                    '/var/run/dropbear.pid',
                    'kill -0',
                    'test "$($busy cat "$fingerprint")" = "$actual"'):
        require(literal in source, 'real PID1/host identity missing: ' + literal)

    for marker in ('M230_FIRST_HOST_SSH_READY',
                   'M230_REOPEN_HOST_SSH_READY',
                   'M230_FIRST_HOST_SSH_WINDOW_COMPLETE',
                   'M230_REOPEN_HOST_SSH_WINDOW_COMPLETE'):
        require(source.count(marker) == 1, 'host SSH window marker duplicate: ' + marker)
    require('$busy sleep 150' in source,
            'guest must remain alive long enough for slow encrypted handshake')
    require(not re.search(r'(?m)^\s*(?:ssh|dbclient|nc|socat|python3?)\b', source),
            'guest may not manufacture the independent host SSH proof')
    require('id_ed25519' not in source and
            'm230-client-private' not in source,
            'private client SSH identity must remain exclusively on host')

    require("Path(__file__).with_name('m227-alpine-uart.py')" in uart,
            'reuse existing original Alpine M227 UART owner')
    require('owner.FIRST_COMMANDS[:-2]' in uart and
            'owner.REBOOT_COMMANDS[:1]' in uart,
            'reuse M197 signed packages and original PID1 login')
    for literal in ('apk add dropbear dropbear-openrc',
                    'M230_SIGNED_DROPBEAR_APK_RC=0',
                    '/root/m230-alpine-dropbear-workload.sh --first',
                    '/root/m230-alpine-dropbear-workload.sh --reopen',
                    'M230_FIRST_SCRIPT_RC=0',
                    'M230_REOPEN_SCRIPT_RC=0'):
        require(literal in uart, 'signed package/guest UART missing: ' + literal)


def assert_reject(guest: str, uart: str, label: str) -> None:
    try:
        validate(guest, uart)
    except AssertionError:
        return
    raise AssertionError('[M230 GUEST CONTRACT FAIL] accepted mutant: ' + label)


def main() -> None:
    subprocess.run(['sh', '-n', str(GUEST)], check=True)
    subprocess.run(['python3', '-m', 'py_compile', str(UART)], check=True)
    guest = GUEST.read_text(encoding='utf-8')
    uart = UART.read_text(encoding='utf-8')
    validate(guest, uart)
    assert_reject(guest.replace('="-s -g -j -k -m ', '="-g -j -k -m ', 1),
                  uart, 'password logins accidentally enabled')
    assert_reject(guest.replace('0.0.0.0:15446', '127.0.0.1:15446', 1),
                  uart, 'inbound listener inaccessible from VirtIO NIC')
    assert_reject(guest.replace('rc-service dropbear start ||', 'true ||', 1),
                  uart, 'original OpenRC start bypassed')
    assert_reject(guest.replace('REOPEN)\n        # Never invoke',
                                'REOPEN)\n        rc-service dropbear start\n'
                                '        # Never invoke', 1),
                  uart, 'second boot concealed original OpenRC startup failure')
    assert_reject(guest, uart.replace('apk add dropbear dropbear-openrc',
                                     'true', 1),
                  'original signed Dropbear APK installation bypassed')
    print('[M230 ORIGINAL ALPINE DROPBEAR GUEST CONTRACT PASS] vendor OpenRC, '
          'signed ELF, root-only restricted Ed25519 public key, IPv4 listener, '
          'two true PID1 boots, no manual second start, independent host SSH '
          'window; NOT native QEMU/SSH/ext4 acceptance')


if __name__ == '__main__':
    main()
