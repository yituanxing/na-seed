#!/usr/bin/env python3
"""Real three-ISA QEMU shell argument builders; host-only, no QEMU launch.

The netdev fragment is extracted from the production runner, sourced and
executed by /bin/sh. Assertions check exact argv boundaries and failure before
any device option is committed, including the M228/M233 legacy ABI.
"""
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
RUNNERS = (
    ('scripts/run.sh', 'virtio-net-pci,netdev=naseed_net,disable-legacy=on',
     'virtio-net-pci,netdev=naseed_net2,disable-legacy=on'),
    ('scripts/probe-riscv64-busybox-matrix.sh',
     'virtio-net-device,netdev=naseed_net',
     'virtio-net-device,netdev=naseed_net2'),
    ('scripts/probe-aarch64-busybox-matrix.sh',
     'virtio-net-device,netdev=naseed_net',
     'virtio-net-device,netdev=naseed_net2'),
)
SELECTORS = (
    'NAS_VIRTIO_NET_USER', 'NAS_VIRTIO_NET_SECOND_USER',
    'NAS_VIRTIO_NET_PCAP', 'NAS_VIRTIO_NET_HOSTFWD_PORT',
    'NAS_VIRTIO_NET_GUESTFWD_PORT', 'NAS_VIRTIO_NET_HOSTFWD_UDP_PORT',
    'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT', 'NAS_VIRTIO_NET_EXTRA_HOSTFWD',
)


def exercise(path: str, **settings: str) -> subprocess.CompletedProcess:
    source = (ROOT / path).read_text()
    start = source.index('# M228:')
    end = source.index('if [ "${NAS_QEMU_INPUT_OPTIN:-0}" = 1 ]; then', start)
    fragment = source[start:end]
    # Caller-owned argv, including the QEMU-specific -device model, is the
    # actual production shell: no alternate test-only network implementation.
    shell = ('set -eu\nprobe() {\n  set --\n' + fragment +
             '\n  for arg do printf "ARG:%s\\n" "$arg"; done\n}\nprobe\n')
    env = dict(os.environ)
    for key in SELECTORS:
        env.pop(key, None)
    env.update(settings)
    return subprocess.run(['sh', '-c', shell], cwd=ROOT, env=env,
                          text=True, capture_output=True, check=False)


def exact(path: str, expected: list[str], **settings: str) -> None:
    result = exercise(path, **settings)
    assert result.returncode == 0, (path, settings, result.stderr)
    assert result.stdout.splitlines() == ['ARG:' + arg for arg in expected], (
        path, settings, result.stdout)


def rejected(path: str, diagnostic: str, **settings: str) -> None:
    result = exercise(path, **settings)
    assert result.returncode == 2 and result.stdout == '', (
        path, settings, result.returncode, result.stdout)
    assert diagnostic in result.stderr, (path, settings, result.stderr)


def check() -> None:
    for path in ('scripts/acceptance/qemu-netdev.sh',
                 'scripts/qemu-hostfwd-extra.sh',
                 *(row[0] for row in RUNNERS)):
        subprocess.run(['sh', '-n', path], cwd=ROOT, check=True)
    for path, first_device, second_device in RUNNERS:
        exact(path, [])
        exact(path, [
            '-netdev', 'user,id=naseed_net', '-device', first_device,
        ], NAS_VIRTIO_NET_USER='1')
        exact(path, [
            '-netdev', 'user,id=naseed_net', '-device', first_device,
            '-netdev', 'user,id=naseed_net2,net=10.0.3.0/24',
            '-device', second_device,
            '-object', 'filter-dump,id=naseed_wire_capture,'
                       'netdev=naseed_net,file=/tmp/m233 wire.pcap',
        ], NAS_VIRTIO_NET_USER='1', NAS_VIRTIO_NET_SECOND_USER='1',
           NAS_VIRTIO_NET_PCAP='/tmp/m233 wire.pcap')
        exact(path, [
            '-netdev', 'user,id=naseed_net'
                       ',hostfwd=tcp:127.0.0.1:15432-:15432'
                       ',hostfwd=udp:127.0.0.1:15453-:15453'
                       ',hostfwd=tcp:127.0.0.1:15447-:15447'
                       ',hostfwd=tcp:127.0.0.1:15448-:15448',
            '-device', first_device,
        ], NAS_VIRTIO_NET_USER='1', NAS_VIRTIO_NET_HOSTFWD_PORT='00015432',
           NAS_VIRTIO_NET_HOSTFWD_UDP_PORT='15453',
           NAS_VIRTIO_NET_EXTRA_HOSTFWD='tcp:15447:15447,tcp:15448:15448')
        for extra in ('NAS_VIRTIO_NET_SECOND_USER', 'NAS_VIRTIO_NET_PCAP'):
            rejected(path, 'requires NAS_VIRTIO_NET_USER=1', **{extra: '1'})
        for host, guest in (
            ('NAS_VIRTIO_NET_HOSTFWD_PORT', 'NAS_VIRTIO_NET_GUESTFWD_PORT'),
            ('NAS_VIRTIO_NET_HOSTFWD_UDP_PORT',
             'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT'),
        ):
            rejected(path, 'requires ' + host, **{guest: '20'})
            rejected(path, 'requires NAS_VIRTIO_NET_USER=1', **{host: '20'})
            for bad in ('0', '65536', '-22', '20,hostfwd=', 'abc', '000000'):
                rejected(path, host, NAS_VIRTIO_NET_USER='1', **{host: bad})
                rejected(path, guest, NAS_VIRTIO_NET_USER='1',
                         **{host: '80', guest: bad})
        rejected(path, 'duplicate host protocol/port binding',
                 NAS_VIRTIO_NET_USER='1',
                 NAS_VIRTIO_NET_HOSTFWD_PORT='0015445',
                 NAS_VIRTIO_NET_EXTRA_HOSTFWD='tcp:15445:22')
        rejected(path, 'NAS_VIRTIO_NET_EXTRA_HOSTFWD',
                 NAS_VIRTIO_NET_USER='1',
                 NAS_VIRTIO_NET_EXTRA_HOSTFWD='udp:33:22,')
    print('[QEMU NETDEV SHARED CONTRACT PASS] identical original three-ISA '
          'argv, independent second NIC and PCAP; legacy TCP/UDP, extras '
          'and fail-closed option bounds tested without launching QEMU')


if __name__ == '__main__':
    check()
