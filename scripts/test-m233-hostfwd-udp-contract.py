#!/usr/bin/env python3
"""Pure host M233 contract: original three QEMU shell builders, no guest.

Verify the optional loopback-only UDP slirp rule shares the existing NIC,
coexists with M228 TCP rules, and rejects option-injection/invalid ports.
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
BAD_PORTS = ('0', '0000', '65536', '-1', '1e3', '20:80',
             '443,hostfwd=udp::7777-:22', '1000000000000000000000',
             '22\n-device', '65535x', ' 443')


def shell_args(path: str, enabled: bool, udp_host: str | None = None,
               udp_guest: str | None = None,
               tcp_host: str | None = None,
               tcp_guest: str | None = None) -> subprocess.CompletedProcess:
    source = (ROOT / path).read_text()
    start = source.index('# M228:')
    end = source.index(SECOND_NET, start)
    fragment = source[start:end]
    script = ('set -eu\nprobe() {\n  set --\n' + fragment +
              '\n  for arg do printf "ARG:%s\\n" "$arg"; done\n}\nprobe\n')
    env = dict(os.environ)
    for name in ('NAS_VIRTIO_NET_HOSTFWD_PORT',
                 'NAS_VIRTIO_NET_GUESTFWD_PORT',
                 'NAS_VIRTIO_NET_HOSTFWD_UDP_PORT',
                 'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT'):
        env.pop(name, None)
    env['NAS_VIRTIO_NET_USER'] = '1' if enabled else '0'
    for key, value in (
        ('NAS_VIRTIO_NET_HOSTFWD_PORT', tcp_host),
        ('NAS_VIRTIO_NET_GUESTFWD_PORT', tcp_guest),
        ('NAS_VIRTIO_NET_HOSTFWD_UDP_PORT', udp_host),
        ('NAS_VIRTIO_NET_GUESTFWD_UDP_PORT', udp_guest),
    ):
        if value is not None:
            env[key] = value
    return subprocess.run(['sh', '-c', script], env=env,
                          capture_output=True, text=True, check=False)


def expect(path: str, device: str, *, udp_host: str | None = None,
           udp_guest: str | None = None, tcp_host: str | None = None,
           tcp_guest: str | None = None,
           expected_udp_host: str | None = None,
           expected_udp_guest: str = '15453',
           expected_tcp_host: str | None = None,
           expected_tcp_guest: str = '15432') -> None:
    result = shell_args(path, True, udp_host, udp_guest, tcp_host, tcp_guest)
    assert result.returncode == 0, (path, result.stderr)
    netdev = 'ARG:user,id=naseed_net'
    if expected_tcp_host is not None:
        netdev += (',hostfwd=tcp:127.0.0.1:' + expected_tcp_host +
                   '-:' + expected_tcp_guest)
    if expected_udp_host is not None:
        netdev += (',hostfwd=udp:127.0.0.1:' + expected_udp_host +
                   '-:' + expected_udp_guest)
    assert result.stdout.splitlines() == [
        'ARG:-netdev', netdev, 'ARG:-device', 'ARG:' + device,
    ], (path, result.stdout)


def check() -> None:
    helper = (ROOT / 'scripts/acceptance/qemu-netdev.sh').read_text()
    assert 'nas_qemu_netdev_legacy_pair tcp' in helper
    assert 'nas_qemu_netdev_legacy_pair udp' in helper
    assert 'hostfwd=${nas_qemu_netdev_proto}:127.0.0.1:' in helper
    for path, device in RUNNERS:
        subprocess.run(['sh', '-n', str(ROOT / path)], check=True)
        source = (ROOT / path).read_text()
        assert source.count('. scripts/acceptance/qemu-netdev.sh') == 1, path
        assert source.count('nas_qemu_netdev_prepare') == 1, path
        expect(path, device)  # Existing plain NIC unchanged.
        expect(path, device, tcp_host='15446', expected_tcp_host='15446')
        for requested, normalized in (
            ('1', '1'), ('00080', '80'), ('15453', '15453'),
            ('65535', '65535'),
        ):
            expect(path, device, udp_host=requested,
                   expected_udp_host=normalized)
            expect(path, device, udp_host='15453', udp_guest=requested,
                   expected_udp_host='15453', expected_udp_guest=normalized)
        # TCP and UDP are separate protocol rules on ONE NIC, in fixed order.
        expect(path, device, udp_host='005353', udp_guest='00053',
               tcp_host='008080', tcp_guest='015445',
               expected_udp_host='5353', expected_udp_guest='53',
               expected_tcp_host='8080', expected_tcp_guest='15445')
        for invalid in BAD_PORTS:
            for param in ('host', 'guest'):
                options = ({'udp_host': invalid} if param == 'host' else
                           {'udp_host': '15453', 'udp_guest': invalid})
                result = shell_args(path, True, **options)
                assert result.returncode != 0 and not result.stdout, (path, invalid)
                token = ('NAS_VIRTIO_NET_HOSTFWD_UDP_PORT' if param == 'host' else
                         'NAS_VIRTIO_NET_GUESTFWD_UDP_PORT')
                assert token in result.stderr, (path, result.stderr)
        result = shell_args(path, True, udp_guest='15453')
        assert result.returncode != 0 and not result.stdout, path
        assert 'requires NAS_VIRTIO_NET_HOSTFWD_UDP_PORT' in result.stderr
        result = shell_args(path, False, udp_host='15453')
        assert result.returncode != 0 and not result.stdout, path
        assert 'requires NAS_VIRTIO_NET_USER=1' in result.stderr
        result = shell_args(path, False)
        assert result.returncode == 0 and not result.stdout, path
    print('[M233 UDP HOSTFWD CONTRACT PASS] amd64/RV64/AArch64 original QEMU'
          ' builders: default+TCP byte-exact, one native NIC, UDP 127.0.0.1'
          ' opt-in, normalized 1..65535 guest/host ports and fail-closed inputs')


if __name__ == '__main__':
    check()
