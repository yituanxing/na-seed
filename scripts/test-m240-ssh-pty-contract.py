#!/usr/bin/env python3
"""Fail-closed M240 SSH PTY contract: no QEMU, guest network or credentials."""

import importlib.util
import hashlib
import json
import io
from pathlib import Path
import subprocess
import sys
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
    assert 'M240-READ-READY:' in (HERE / 'm240-host-ssh-pty-check.py').read_text()
    assert 'M240-SLEEP-NATURAL:' in (HERE / 'm240-host-ssh-pty-check.py').read_text()
    assert 'NAS_M240_SSH_PTY' in workflow
    assert 'NAS_M230_DROPBEAR_HOST' not in workflow

    # A healthy foreground command must remain blocked until genuine ISIG.
    # An instantly failing/nonexistent sleep must NEVER fake the Ctrl-C proof.
    broken = subprocess.Popen(['sh', '-c', "printf 'M240-SLEEP-NATURAL:fake\\n'; sleep 2"],
                              stdout=subprocess.PIPE)
    try:
        must_reject(lambda: HOST.confirm_foreground_block(
            broken, b'M240-SLEEP-NATURAL:fake', b'', 0.35))
    finally:
        broken.kill()
        broken.wait()
        broken.stdout.close()
    blocked = subprocess.Popen(['sh', '-c', 'sleep 2'], stdout=subprocess.PIPE)
    try:
        HOST.confirm_foreground_block(blocked, b'M240-SLEEP-NATURAL:fake', b'', 0.15)
    finally:
        blocked.kill()
        blocked.wait()
        blocked.stdout.close()

    # A third-session TCP reset must preserve a bounded host-side diagnostic
    # instead of becoming the indistinguishable generic PTY timeout from the
    # initial native pilot. A banner alone never satisfies a guest receipt.
    reset = subprocess.Popen(['sh', '-c', "printf 'SSH-BANNER-ONLY\\n'"],
                             stdout=subprocess.PIPE)
    try:
        try:
            HOST.recv_until(reset, b'M240-PTY-FDS:YES', 2)
        except TimeoutError as exc:
            assert 'expected=' in str(exc) and 'stdout_tail=' in str(exc)
            assert 'SSH-BANNER-ONLY' in str(exc)
        else:
            raise AssertionError('missing PTY marker accepted as success')
    finally:
        reset.wait(timeout=3)
        reset.stdout.close()

    # The host uses OpenSSH -tt over pipes, without a graphical terminal
    # emulator. A real interactive ash may issue CSI 6n before its prompt is
    # ready; the host must reply to that query, including split read chunks,
    # without treating the reply itself as evidence of the remote PTY proof.
    class FakeSSH:
        stdin = io.BytesIO()

        @staticmethod
        def poll():
            return None

    fake = FakeSSH()
    terminal = HOST.TerminalCursorResponder()
    terminal.observe(fake, b'\r\n~ # \x1b[')
    assert terminal.count == 0 and fake.stdin.getvalue() == b''
    terminal.observe(fake, b'6n')
    assert terminal.count == 1 and fake.stdin.getvalue() == b'\x1b[1;1R'
    terminal.observe(fake, b'ordinary prompt bytes')
    assert terminal.count == 1 and fake.stdin.getvalue() == b'\x1b[1;1R'

    # End-to-end synthetic remote line editor blocks until the exact host
    # cursor reply arrives; it alone cannot fabricate a guest proof marker.
    emulator = subprocess.Popen([
        sys.executable, '-u', '-c',
        "import sys,time; "
        "sys.stdout.buffer.write(b'\\x1b['); sys.stdout.flush(); "
        "time.sleep(.1); "
        "sys.stdout.buffer.write(b'6n'); sys.stdout.flush(); "
        "reply=sys.stdin.buffer.read(6); "
        "sys.stdout.buffer.write(b'\\nDSR-RESPONDED\\n' "
        "if reply==b'\\x1b[1;1R' else b'\\nDSR-BAD\\n'); "
        "sys.stdout.flush()"
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        response = HOST.recv_until(emulator, b'DSR-RESPONDED', 3,
                                   terminal=HOST.TerminalCursorResponder())
        assert b'\nDSR-RESPONDED\n' in response
        assert emulator.wait(timeout=2) == 0
    finally:
        if emulator.poll() is None:
            emulator.kill()
        emulator.wait(timeout=2)
        emulator.stdin.close()
        emulator.stdout.close()

    # Real ash can print its prompt and then block on DSR before it is
    # prepared to read the *first* command. Synchronize shell readiness
    # independently; no receipt may be inferred from a prompt or query.
    starting = subprocess.Popen([
        sys.executable, '-u', '-c',
        "import sys,time; "
        "sys.stdout.buffer.write(b'\\n~ # \\x1b['); sys.stdout.flush(); "
        "time.sleep(.1); "
        "sys.stdout.buffer.write(b'6n'); sys.stdout.flush(); "
        "reply=sys.stdin.buffer.read(6); "
        "time.sleep(.05); "
        "sys.stdout.buffer.write(b'\\n~ # '); sys.stdout.flush(); "
        "line=sys.stdin.buffer.readline(); "
        "sys.stdout.buffer.write(b'\\nM240-SYNC:' + line "
        "if reply==b'\\x1b[1;1R' else b'\\nM240-BAD\\n'); "
        "sys.stdout.flush()"
    ], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        live_terminal = HOST.TerminalCursorResponder()
        start_output = HOST.wait_shell_ready(starting, 3, live_terminal)
        assert b'\x1b[6n' in start_output and live_terminal.count == 1
        HOST.send(starting, b'challenge\n')
        response = HOST.recv_until(starting, b'M240-SYNC:challenge', 2,
                                   start_output, terminal=live_terminal)
        assert b'M240-SYNC:challenge\n' in response
        assert starting.wait(timeout=2) == 0
    finally:
        if starting.poll() is None:
            starting.kill()
        starting.wait(timeout=2)
        starting.stdin.close()
        starting.stdout.close()

    # A banner and terminal echo alone must not count as a ready guest shell.
    echo_only = subprocess.Popen(['sh', '-c', "printf 'early echoed command\r\n'"],
                                 stdout=subprocess.PIPE, stdin=subprocess.PIPE)
    try:
        must_reject(lambda: HOST.wait_shell_ready(
            echo_only, 0.4, HOST.TerminalCursorResponder()))
    finally:
        echo_only.wait(timeout=2)
        echo_only.stdin.close()
        echo_only.stdout.close()

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
        nonces = ['%048x' % (i + 1) for i in range(HOST.SESSION_COUNT)]
        sessions = [
            {'session': i + 1, 'nonce': nonces[i],
             'nonce_sha256': hashlib.sha256(nonces[i].encode()).hexdigest(),
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
