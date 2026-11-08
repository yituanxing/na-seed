#!/usr/bin/env python3
"""M240 two native-QEMU original Alpine Dropbear PTY + physical ext4 oracle."""

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


def load(filename: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError('M240 requires canonical M230 SSH/original Alpine oracle')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M230 = load('m230-dropbear-disk-check.py', 'm240_m230_oracle')
HOST = load('m240-host-ssh-pty-check.py', 'm240_host_pty_oracle')
POLICY = b'no-port-forwarding,no-agent-forwarding,no-X11-forwarding'
HEX64 = re.compile(r'[0-9a-f]{64}\Z')


def fail(message: str) -> None:
    raise ValueError('[M240 ORIGINAL SSH PTY FAIL] ' + message)


def receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail(phase + ' missing independent host SSH PTY receipt')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(phase + ' malformed host SSH PTY receipt: ' + str(exc))
    if not isinstance(data, dict):
        fail(phase + ' receipt must be a JSON object')
    fixed = {'phase': phase, 'transport': HOST.TRANSPORT,
             'host': '127.0.0.1', 'guest_port': M230.HOST.GUEST_PORT,
             'pty_allocated': True, 'public_key_only': True,
             'root_only_isolated_test_no_DAC': True,
             'session_count': HOST.SESSION_COUNT}
    for key, value in fixed.items():
        if type(data.get(key)) is not type(value) or data[key] != value:
            fail(phase + ' invalid SSH PTY proof: ' + key)
    host_port = data.get('host_port')
    if type(host_port) is not int or not 1 <= host_port <= 65535:
        fail(phase + ' invalid localhost forwarded SSH port')
    if not isinstance(data.get('host_key_fingerprint'), str) or not M230.HOST.FINGERPRINT.fullmatch(data['host_key_fingerprint']):
        fail(phase + ' missing pinned Ed25519 Dropbear host key fingerprint')
    if not isinstance(data.get('client_pubkey_sha256'), str) or not HEX64.fullmatch(data['client_pubkey_sha256']):
        fail(phase + ' missing disposable client public key fingerprint')
    if not isinstance(data.get('timestamp_utc'), str) or not M230.TIME_PATTERN.fullmatch(data['timestamp_utc']):
        fail(phase + ' missing host PTY observation time')
    sessions = data.get('sessions')
    if not isinstance(sessions, list) or len(sessions) != HOST.SESSION_COUNT:
        fail(phase + ' did not record all nine SSH PTY reconnects')
    nonces = set()
    for index, row in enumerate(sessions, 1):
        if not isinstance(row, dict) or type(row.get('session')) is not int or row['session'] != index:
            fail(phase + ' SSH PTY session sequence invalid')
        for flag in ('tty_allocated', 'canonical_backspace', 'foreground_sigint',
                     'winsize_33x101'):
            if row.get(flag) is not True:
                fail(phase + ' SSH PTY session lacks remote ' + flag)
        for sha in ('nonce_sha256', 'remote_stdout_sha256'):
            value = row.get(sha)
            if not isinstance(value, str) or not HEX64.fullmatch(value):
                fail(phase + ' host PTY session lacks ' + sha)
        nonces.add(row['nonce_sha256'])
    if len(nonces) != HOST.SESSION_COUNT:
        fail(phase + ' guest SSH PTY challenge nonces reused')
    return data


def check_ext4(port: str, source: Path, disk: Path, client_sha: str) -> dict:
    for name in ('/root/.ssh/authorized_keys',
                 '/etc/dropbear/dropbear_ed25519_host_key',
                 '/etc/runlevels/default/dropbear'):
        path = M230.M191.guest_path(source, name)
        if path.exists() or path.is_symlink():
            fail('signed Alpine baseline was preseeded with SSH data: ' + name)
    # This independently enforces original M227/OpenRC + original M197 signed
    # packages and the *real* physical ext4 read-only fsck chain.
    M230.M227.verify(port, source, disk)
    with tempfile.TemporaryDirectory(prefix='m240-dropbear-pty-physical-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        M230.M191.extract_partition(disk, partition)
        def read(name: str) -> bytes:
            return M230.M191.debugfs_cat(partition, name)
        def stat(name: str) -> str:
            return subprocess.run(['debugfs', '-R', 'stat ' + name, str(partition)],
                                  capture_output=True, text=True, check=True).stdout
        installed = read('/lib/apk/db/installed')
        world = read('/etc/apk/world').decode('utf-8')
        for package in M230.PACKAGES:
            if (package not in world.splitlines() or
                    b'\nP:' + package.encode() + b'\n' not in b'\n' + installed):
                fail('original signed Alpine APK missing ' + package)
        original = read('/usr/sbin/dropbear')
        if (len(original) < 20 or original[:6] != b'\x7fELF\x02\x01' or
                struct.unpack_from('<H', original, 18)[0] != M230.M191.ARCH_MACHINE[port]):
            fail('original signed Dropbear guest ELF missing or incorrect ISA')
        if not read('/etc/init.d/dropbear').startswith(b'#!'):
            fail('original signed Dropbear OpenRC service missing')
        runlevel = stat('/etc/runlevels/default/dropbear')
        if 'Type: symlink' not in runlevel or 'dropbear' not in runlevel:
            fail('automatic original Dropbear default runlevel not durable')
        authorized = read('/root/.ssh/authorized_keys').strip().splitlines()
        if len(authorized) != 1 or authorized[0].split()[:2] != [POLICY, b'ssh-ed25519']:
            fail('original Dropbear allowed-PTY key must forbid all forwarding')
        fields = authorized[0].split()
        if len(fields) != 3 or hashlib.sha256(b' '.join(fields[1:]) + b'\n').hexdigest() != client_sha:
            fail('persisted Ed25519 key differs from genuine host client')
        hostkey = read('/etc/dropbear/dropbear_ed25519_host_key')
        mode = stat('/etc/dropbear/dropbear_ed25519_host_key')
        if (len(hostkey) < 80 or 'Type: regular' not in mode or
                re.search(r'Mode:\s+0*600\b', mode) is None):
            fail('persisted original Dropbear private host key missing secure mode')
        return {'original_signed_dropbear_ELF': True, 'original_signed_APKs': list(M230.PACKAGES),
                'original_openrc_auto_restart': True,
                'host_client_public_key_sha256': client_sha,
                'persisted_original_dropbear_hostkey_sha256': hashlib.sha256(hostkey).hexdigest(),
                'root_only_no_production_DAC_claim': True}


def verify(port: str, root: Path, disk: Path, first: Path, reopen: Path,
           first_pcap: Path, reopen_pcap: Path) -> dict:
    files = (root, disk, first, reopen, first_pcap, reopen_pcap)
    if (any(p.is_symlink() or not p.exists() for p in files) or
            len({str(p.resolve(strict=False)) for p in files}) != len(files)):
        fail('signed root, ext4 disk and two independent host proofs invalid or overlap')
    a, b = receipt(first, 'first'), receipt(reopen, 'reopen')
    if (a['host_port'] != b['host_port'] or
            a['client_pubkey_sha256'] != b['client_pubkey_sha256'] or
            a['host_key_fingerprint'] != b['host_key_fingerprint'] or
            a['timestamp_utc'] >= b['timestamp_utc'] or
            a['sessions'][0]['nonce_sha256'] == b['sessions'][0]['nonce_sha256']):
        fail('two QEMU boots do not preserve host key or fresh PTY session evidence')
    captures = (first_pcap.read_bytes(), reopen_pcap.read_bytes())
    if (not all(captures) or
            hashlib.sha256(captures[0]).digest() == hashlib.sha256(captures[1]).digest()):
        fail('independent physical guest NIC captures absent or identical')
    flows = []
    for phase, pcap in zip(('first', 'reopen'), captures):
        flow = M230.physical_ssh_flow(pcap, phase)
        flow['pcap_sha256'] = hashlib.sha256(pcap).hexdigest()
        if flow['matching_guest_ssh_tcp_sessions'] < HOST.SESSION_COUNT:
            fail(phase + ' NIC does not contain nine real OpenSSH/Dropbear TCP sessions')
        flows.append(flow)
    original = check_ext4(port, root, disk, a['client_pubkey_sha256'])
    return {'acceptance': 'M240-original-Alpine-2xPID1-OpenRC-real-OpenSSH-PTY-9x2-physical-ext4',
            'port': port, 'genuine_pty_sessions_each_boot': HOST.SESSION_COUNT,
            'independent_pinned_host_identity_across_two_reboots': True,
            'actual_physical_guest_eth0_ssh_pcap': {'first': flows[0], 'reopen': flows[1]},
            'original_signed_alpine_ext4': original,
            'root_only_isolated_test_not_production_security': True}


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m240-ssh-pty-disk-check.py ISA SIGNED_ROOT DISK '
                         'FIRST.json REOPEN.json FIRST.pcap REOPEN.pcap OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, pcap1, pcap2, output = map(Path, sys.argv[2:])
    if output.exists() or output.is_symlink() or any(
            output.resolve(strict=False) == item.resolve(strict=False)
            for item in (root, disk, first, reopen, pcap1, pcap2)):
        fail('refusing to overwrite stale or overlapping final oracle')
    data = verify(port, root, disk, first, reopen, pcap1, pcap2)
    output.write_text(json.dumps(data, sort_keys=True, indent=2) + '\n')
    print(f'[M240 ORIGINAL ALPINE SSH PTY TWO BOOT PASS] {port}: '
          '9 interactive PTYs on EACH original signed Alpine PID1 boot, '
          'genuine host SSH/guest NIC + persisted host key, canonical delete, '
          'foreground Ctrl-C, source OpenRC/ELF, two PCAPs, ext4/fsck; '
          'disposable root-only validation, NOT production security')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        raise SystemExit('[M240 FINAL ORIGINAL SSH PTY ORACLE FAIL] ' + str(exc)) from exc
