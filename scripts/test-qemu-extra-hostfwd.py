#!/usr/bin/env python3
"""Execute the real three-ISA shell builders for multiple slirp host forwards.

This is a pure host contract: no QEMU, synthetic guest or fake service PASS.
"""
from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parent.parent
RUNNERS = (
    ('scripts/run.sh', 'virtio-net-pci,netdev=naseed_net,disable-legacy=on'),
    ('scripts/probe-riscv64-busybox-matrix.sh', 'virtio-net-device,netdev=naseed_net'),
    ('scripts/probe-aarch64-busybox-matrix.sh', 'virtio-net-device,netdev=naseed_net'),
)
SECOND_NET = 'if [ "${NAS_VIRTIO_NET_SECOND_USER:-0}" = 1 ]; then'
PORT_NAMES = (
    'NAS_VIRTIO_NET_HOSTFWD_PORT', 'NAS_VIRTIO_NET_GUESTFWD_PORT',
    'NAS_VIRTIO_NET_HOSTFWD_UDP_PORT', 'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT',
    'NAS_VIRTIO_NET_EXTRA_HOSTFWD',
)


def build(path: str, enabled: bool = True, *, extra: str | None = None,
          tcp: str | None = None, udp: str | None = None) -> subprocess.CompletedProcess:
    source = (ROOT / path).read_text()
    fragment = source[source.index('# M228:'):source.index(SECOND_NET)]
    script = ('set -eu\nprobe() {\n  set --\n' + fragment +
              '\n  for arg do printf "ARG:%s\\n" "$arg"; done\n}\nprobe\n')
    env = dict(os.environ)
    for name in PORT_NAMES:
        env.pop(name, None)
    env['NAS_VIRTIO_NET_USER'] = '1' if enabled else '0'
    for name, value in (
        ('NAS_VIRTIO_NET_EXTRA_HOSTFWD', extra),
        ('NAS_VIRTIO_NET_HOSTFWD_PORT', tcp),
        ('NAS_VIRTIO_NET_HOSTFWD_UDP_PORT', udp),
    ):
        if value is not None:
            env[name] = value
    return subprocess.run(['sh', '-c', script], cwd=ROOT, env=env,
                          text=True, capture_output=True, check=False)


def assert_netdev(path: str, device: str, *, extra: str | None = None,
                  tcp: str | None = None, udp: str | None = None,
                  expected: str = '') -> None:
    result = build(path, extra=extra, tcp=tcp, udp=udp)
    assert result.returncode == 0, (path, result.stderr)
    assert result.stdout.splitlines() == [
        'ARG:-netdev', 'ARG:user,id=naseed_net' + expected,
        'ARG:-device', 'ARG:' + device,
    ], (path, result.stdout)


def check() -> None:
    subprocess.run(['sh', '-n', str(ROOT / 'scripts/qemu-hostfwd-extra.sh')], check=True)
    for path, device in RUNNERS:
        subprocess.run(['sh', '-n', str(ROOT / path)], check=True)
        assert_netdev(path, device)
        assert_netdev(path, device, tcp='15453', udp='15453', expected=(
            ',hostfwd=tcp:127.0.0.1:15453-:15432'
            ',hostfwd=udp:127.0.0.1:15453-:15453'))
        assert_netdev(path, device, extra=(
            'tcp:15447:15447,tcp:15448:15448,udp:15453:15453'), expected=(
            ',hostfwd=tcp:127.0.0.1:15447-:15447'
            ',hostfwd=tcp:127.0.0.1:15448-:15448'
            ',hostfwd=udp:127.0.0.1:15453-:15453'))
        assert_netdev(path, device, tcp='15453', udp='15453',
                      extra='tcp:0015447:15447,tcp:15448:0015448', expected=(
                          ',hostfwd=tcp:127.0.0.1:15453-:15432'
                          ',hostfwd=udp:127.0.0.1:15453-:15453'
                          ',hostfwd=tcp:127.0.0.1:15447-:15447'
                          ',hostfwd=tcp:127.0.0.1:15448-:15448'))
        # Same numeric host port can be TCP and UDP, but must not collide
        # twice inside one protocol or with a legacy forwarding rule.
        assert_netdev(path, device, extra='tcp:0099:22,udp:99:53', expected=(
            ',hostfwd=tcp:127.0.0.1:99-:22'
            ',hostfwd=udp:127.0.0.1:99-:53'))
        bad = (
            'tcp:0:22', 'tcp:65536:22', 'tcp:22:000', 'tcp:22:65536',
            'tcp:1e3:22', 'tcp:80:80,', ',tcp:80:80', 'tcp:80:80,,udp:81:81',
            'tcp:80:80; -device', 'tcp:80:80\n-device',
            'tcp:80:80,hostfwd=tcp:1:1', 'tcp:80:80, tcp:81:81',
            'tcp:80:80,tcp:080:81', 'tcp:80:80:80', 'icmp:80:80',
            'tcp::80', 'udp:80:', 'tcp:80', 'tcp:80:81,',
            ','.join(f'tcp:{i}:1' for i in range(1, 14)),
        )
        for extra in bad:
            result = build(path, extra=extra)
            assert result.returncode != 0 and not result.stdout, (path, extra)
            assert 'NAS_VIRTIO_NET_EXTRA_HOSTFWD' in result.stderr, (path, extra)
        for extra, tcp, udp in (
            ('tcp:0015445:77', '15445', None),
            ('udp:015453:88', None, '15453'),
        ):
            result = build(path, extra=extra, tcp=tcp, udp=udp)
            assert result.returncode != 0 and not result.stdout
        result = build(path, False, extra='tcp:80:80')
        assert result.returncode != 0 and not result.stdout
        assert 'requires NAS_VIRTIO_NET_USER=1' in result.stderr
        result = build(path, False)
        assert result.returncode == 0 and not result.stdout
    print('[MULTI HOSTFWD CONTRACT PASS] x86-64/RV64/AArch64 original shell '
          'builders: unchanged defaults, one real NIC, bounded extra TCP/UDP '
          'loopback-only mappings and malformed/duplicate fail-closed')


if __name__ == '__main__':
    check()
