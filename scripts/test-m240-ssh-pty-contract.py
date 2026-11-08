#!/usr/bin/env python3
"""Fail-closed M240 SSH PTY contract: no QEMU, guest network or credentials."""

import importlib.util
import json
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def load(name, alias):
    spec = importlib.util.spec_from_file_location(alias, HERE / name)
    if spec is None or spec.loader is None:
        raise RuntimeError(name)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


HOST = load('m240-host-ssh-pty-check.py', 'm240_test_host')
DISK = load('m240-ssh-pty-disk-check.py', 'm240_test_disk')


def must_reject(callable_):
    try:
        callable_()
    except (ValueError, OSError, KeyError):
        return
    raise AssertionError('M240 negative input was not rejected')


def main():
    old_guest = (ROOT / 'tests/m230-alpine-dropbear-workload.sh').read_text()
    guest = (ROOT / 'tests/m240-alpine-ssh-pty-workload.sh').read_text()
    runner = (HERE / 'probe-m197-alpine-software-cohort.sh').read_text()
    uart = (HERE / 'm240-alpine-uart.py').read_text()
    workflow = (ROOT / '.github/workflows/m240-alpine-ssh-pty.yml').read_text()
    assert "printf 'restrict ssh-ed25519 %s\\n'" in old_guest
    assert "key_policy=no-port-forwarding,no-agent-forwarding,no-X11-forwarding" in guest
    assert "printf 'restrict,pty" not in guest
    assert 'rc-service dropbear start ||' in guest.split('REOPEN)')[0]
    assert 'rc-service dropbear start ||' not in guest.split('REOPEN)')[1].split('esac')[0]
    assert '[ "$m240" = 1 ]' in runner and 'ssh_host_checker=scripts/m240-host-ssh-pty-check.py' in runner
    assert 'ssh_disk_checker=scripts/m240-ssh-pty-disk-check.py' in runner
    assert 'M240_FIRST_HOST_SSH_PTY_READY' in guest
    assert 'M240_REOPEN_HOST_SSH_PTY_READY' in guest
    assert 'M240_FIRST_SCRIPT_RC=0' in uart and 'M240_REOPEN_SCRIPT_RC=0' in uart
    assert 'NAS_M240_SSH_PTY' in workflow
    assert 'NAS_M230_DROPBEAR_HOST' not in workflow

    with tempfile.TemporaryDirectory(prefix='m240-pty-contract-') as tmp:
        base = Path(tmp)
        key = base / 'client-key'
        hosts = base / 'known_hosts'
        argv = HOST.ssh_argv('first', 12222, key, hosts)
        assert '-tt' in argv and '-T' not in argv
        assert '-o' in argv and 'ClearAllForwardings=yes' in argv
        assert 'RequestTTY=no' not in argv and 'PasswordAuthentication=no' in argv
        assert 'StrictHostKeyChecking=accept-new' in argv
        assert 'StrictHostKeyChecking=yes' in HOST.ssh_argv('reopen', 12222, key, hosts)

        path = base / 'first.json'
        must_reject(lambda: DISK.receipt(path, 'first'))
        nonces = ['%064x' % (i + 1) for i in range(HOST.SESSION_COUNT)]
        sessions = [
            {'session': i + 1, 'nonce_sha256': nonces[i],
             'remote_stdout_sha256': '%064x' % (i + 100),
             'tty_allocated': True, 'canonical_backspace': True,
             'foreground_sigint': True, 'winsize_33x101': True}
            for i in range(HOST.SESSION_COUNT)
        ]
        valid = {'phase': 'first', 'transport': HOST.TRANSPORT,
                 'host': '127.0.0.1', 'guest_port': HOST.BASE.GUEST_PORT,
                 'host_port': 12222, 'pty_allocated': True,
                 'public_key_only': True, 'root_only_isolated_test_no_DAC': True,
                 'session_count': HOST.SESSION_COUNT, 'sessions': sessions,
                 'client_pubkey_sha256': 'a' * 64,
                 'host_key_fingerprint': 'SHA256:' + 'A' * 43,
                 'timestamp_utc': '2026-11-08T00:00:00Z'}

        def write(row):
            path.write_text(json.dumps(row))
        write(valid)
        assert DISK.receipt(path, 'first')['session_count'] == HOST.SESSION_COUNT
        for field, replacement in (
                ('pty_allocated', False), ('session_count', HOST.SESSION_COUNT - 1),
                ('public_key_only', False), ('transport', 'fake-ssh')):
            corrupted = dict(valid, **{field: replacement})
            write(corrupted)
            must_reject(lambda: DISK.receipt(path, 'first'))
        for mutation in ('deleted', 'duplicate', 'sigint', 'canonical', 'winsize'):
            rows = [dict(s) for s in sessions]
            if mutation == 'deleted':
                rows.pop()
            elif mutation == 'duplicate':
                rows[1]['nonce_sha256'] = rows[0]['nonce_sha256']
            elif mutation == 'sigint':
                rows[1]['foreground_sigint'] = False
            elif mutation == 'canonical':
                rows[1]['canonical_backspace'] = False
            elif mutation == 'winsize':
                rows[1]['winsize_33x101'] = False
            write(dict(valid, sessions=rows))
            must_reject(lambda: DISK.receipt(path, 'first'))

    print('[M240 SSH PTY HOST CONTRACT PASS] independent selector, original '
          'M230 restriction, real OpenSSH -tt, session/nonce/ISIG/winsize '
          'negative receipts, no real QEMU PASS claimed')


if __name__ == '__main__':
    main()
