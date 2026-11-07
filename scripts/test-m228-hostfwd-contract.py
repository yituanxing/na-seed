#!/usr/bin/env python3
"""Host-only M228 opt-in QEMU slirp forwarding semantics, no guest execution.

Exercise the ORIGINAL shell argument-builder fragments rather than a Python
reimplementation.  Historical default devices must remain byte-for-byte the
same; malformed environment values must not inject QEMU options.
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


def original_shell_args(path: str, enabled: bool, port: str | None,
                        guest_port: str | None = None) -> subprocess.CompletedProcess:
    source = (ROOT / path).read_text()
    start = source.index('# M228:')
    stop = source.index(SECOND_NET, start)
    fragment = source[start:stop]
    # The extracted code is precisely the existing sh positional-argument
    # builder, in its original language, with every other hardware option
    # excluded to keep this host-only contract fast and deterministic.
    script = ('set -eu\nprobe() {\n  set --\n' + fragment +
              '\n  for arg do printf "ARG:%s\\n" "$arg"; done\n}\nprobe\n')
    env = dict(os.environ)
    env['NAS_VIRTIO_NET_USER'] = '1' if enabled else '0'
    env.pop('NAS_VIRTIO_NET_HOSTFWD_PORT', None)
    env.pop('NAS_VIRTIO_NET_GUESTFWD_PORT', None)
    if port is not None:
        env['NAS_VIRTIO_NET_HOSTFWD_PORT'] = port
    if guest_port is not None:
        env['NAS_VIRTIO_NET_GUESTFWD_PORT'] = guest_port
    return subprocess.run(['sh', '-c', script], env=env, capture_output=True,
                          text=True, check=False)


def check() -> None:
    for path, device in RUNNERS:
        subprocess.run(['sh', '-n', str(ROOT / path)], check=True)
        source = (ROOT / path).read_text()
        assert source.count('hostfwd=tcp:127.0.0.1:') == 1, path
        default = original_shell_args(path, enabled=True, port=None)
        assert default.returncode == 0, (path, default.stderr)
        assert default.stdout.splitlines() == [
            'ARG:-netdev', 'ARG:user,id=naseed_net',
            'ARG:-device', 'ARG:' + device], (path, default.stdout)
        absent = original_shell_args(path, enabled=False, port=None)
        assert absent.returncode == 0 and not absent.stdout, path
        for requested, canonical in (('1', '1'), ('80', '80'),
                                     ('00080', '80'), ('8080', '8080'),
                                     ('65535', '65535')):
            result = original_shell_args(path, enabled=True, port=requested)
            assert result.returncode == 0, (path, requested, result.stderr)
            expected = f'ARG:user,id=naseed_net,hostfwd=tcp:127.0.0.1:{canonical}-:15432'
            actual = result.stdout.splitlines()
            assert actual == ['ARG:-netdev', expected,
                              'ARG:-device', 'ARG:' + device], (path, actual)
            assert not any('naseed_net2' in line for line in actual), path
        for guest_requested, guest_canonical in (('1', '1'), ('00080', '80'),
                                                 ('15445', '15445'),
                                                 ('65535', '65535')):
            result = original_shell_args(path, enabled=True, port='15446',
                                         guest_port=guest_requested)
            assert result.returncode == 0, (path, guest_requested, result.stderr)
            assert result.stdout.splitlines() == [
                'ARG:-netdev',
                f'ARG:user,id=naseed_net,hostfwd=tcp:127.0.0.1:15446-:{guest_canonical}',
                'ARG:-device', 'ARG:' + device,
            ], (path, guest_requested, result.stdout)
        for invalid in ('0', '0000', '65536', '-1', '1e3', '20:80',
                        '443,hostfwd=tcp::7777-:22', '1000000000000000000000',
                        '22\n-device', '65535x'):
            result = original_shell_args(path, enabled=True, port=invalid)
            assert result.returncode != 0 and not result.stdout, (path, invalid)
            assert 'NAS_VIRTIO_NET_HOSTFWD_PORT' in result.stderr, path
        forbidden_without_nic = original_shell_args(path, enabled=False, port='8080')
        assert forbidden_without_nic.returncode != 0 and not forbidden_without_nic.stdout
        assert 'requires NAS_VIRTIO_NET_USER=1' in forbidden_without_nic.stderr
        for invalid in ('0', '0000', '65536', '-1', '1e3', '20:80',
                        '443,hostfwd=tcp::7777-:22', '1000000000000000000000',
                        '22\n-device', '65535x'):
            result = original_shell_args(path, enabled=True, port='15446',
                                         guest_port=invalid)
            assert result.returncode != 0 and not result.stdout, (path, invalid)
            assert 'NAS_VIRTIO_NET_GUESTFWD_PORT' in result.stderr, path
        forbidden_without_hostport = original_shell_args(
            path, enabled=True, port=None, guest_port='15445')
        assert (forbidden_without_hostport.returncode != 0 and
                not forbidden_without_hostport.stdout)
        assert ('requires NAS_VIRTIO_NET_HOSTFWD_PORT' in
                forbidden_without_hostport.stderr), path
    print('[M228 HOSTFWD CONTRACT PASS] amd64/RV64/AArch64 original shell'
          ' builders: default unchanged, 1..65535 normalized, one real NIC,'
          ' host-only 127.0.0.1 forwarding, selectable guest TCP port,'
          ' invalid values fail closed')


if __name__ == '__main__':
    check()
