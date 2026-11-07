#!/usr/bin/env python3
"""Host-only M227 boundaries: no fake PID1/service/network proof."""
from pathlib import Path
import ast
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def read(relative: str) -> str:
    return (ROOT / relative).read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError('M227 static contract: ' + message)


def check() -> None:
    native = read('scripts/probe-m197-alpine-software-cohort.sh')
    manifest = read('scripts/m227-alpine-boot-cases.tsv')
    uart = read('scripts/m227-alpine-uart.py')
    guest = read('tests/m227-alpine-openrc-workload.sh')
    host = read('scripts/m227-alpine-disk-check.py')
    require(manifest.count('m197-batch\tcpio\t \t-\t-\t/sbin/init') == 1,
            'each native QEMU must enter original /sbin/init')
    require('NAS_M227_INTEGRATED_OPENRC' in native and
            native.count('m227-alpine-boot-cases.tsv') >= 2,
            'single M197 disk owner with same two original PID1 boots')
    require('scripts/m227-alpine-uart.py first' in native and
            'scripts/m227-alpine-uart.py reopen' in native,
            'both UART phases are physical QEMU not synthetic scripts')
    require('owner.M194.run(' in uart and 'owner.build_commands(cache_index=True)' in uart,
            'reuse original M197 signed WAN and M194 authenticated UART')
    require("'lighttpd-openrc'" in uart or 'lighttpd-openrc' in uart,
            'must install official signed lighttpd-openrc package')
    require('rc-update add lighttpd default' in guest and
            'rc-service lighttpd start' in guest,
            'guest first boot performs actual original OpenRC configuration')
    require('auto eth0' in guest and 'iface eth0' in guest and
            'pre-up /bin/sh /root/m227-network-ready.sh' in guest and
            '/sbin/ifquery -i /etc/network/interfaces --list -a' in guest and
            'rc-update add networking boot' in guest and
            'rc-service networking start' in guest,
            'native ifupdown-ng pre-up must validate kernel-owned live DHCP')
    require('/proc/net/naseed-resolv.conf' in guest and
            '/proc/net/dev' in guest and
            'M227_ERROR kernel DHCP/eth0 unavailable' in guest,
            'genuine DHCP/eth0 readiness must fail closed')
    reopen = guest.split('    --reopen)', 1)[1].split('    *)', 1)[0]
    active = '\n'.join(line for line in reopen.splitlines()
                       if not line.lstrip().startswith('#'))
    for forbidden in ('rc-update add', 'rc-service lighttpd start',
                      'rc-service networking start', '/usr/sbin/lighttpd -D',
                      'apk add'):
        require(forbidden not in active, 'second boot secretly starts/reinstalls service')
    require('rc-service lighttpd status' in active and
            'rc-service networking status' in active and
            '/run/lighttpd.pid' in active,
            'second boot must find genuine auto-started networking and HTTP PID')
    require('127.0.0.1:15432/index.txt' in guest and
            '/usr/bin/aria2c' in guest and '/usr/bin/wget' in guest,
            'two original HTTP clients must issue real guest loopback GET')
    require('M197.verify(port, source, disk)' in host and
            'M195.verify(port, source, disk' in host and
            "('networking', 'boot')" in host and
            "('lighttpd', 'default')" in host,
            'reuse Git/SQLite/strict fsck and both physical OpenRC symlinks')
    require('pre-up /bin/sh /root/m227-network-ready.sh' in host and
            '/proc/net/dev' in host and '/proc/net/naseed-resolv.conf' in host,
            'physical ext4 oracle must verify real ifupdown-ng readiness bridge')
    require('hostfwd' not in guest + uart + host and
            'NAS_QEMU_QMP_SOCKET' not in uart,
            'no false host inbound or GUI/QMP claim')
    for shell in ('scripts/probe-m197-alpine-software-cohort.sh',
                  'tests/m227-alpine-openrc-workload.sh'):
        subprocess.run(['sh', '-n', str(ROOT / shell)], check=True)
    for python in ('scripts/m227-alpine-uart.py',
                   'scripts/m227-alpine-disk-check.py'):
        ast.parse(read(python), filename=python)
    print('[M227 HOST STATIC PASS] two true PID1 manifests, signed public '
          'OpenRC service, no second manual start, native GET and exact ext4 oracle')


if __name__ == '__main__':
    check()
